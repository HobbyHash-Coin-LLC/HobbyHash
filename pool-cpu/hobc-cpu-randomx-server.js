'use strict';
/*
 * hobc-cpu-randomx-server — HOBC RandomX CPU stratum pool.
 *
 * Single-algo RandomX pool for the V6 multi-algo race. Speaks the same simple
 * line-delimited JSON stratum as hobc-rxminer (we own both ends), verifies every
 * share with the node's getrandomxhash RPC (consensus hasher), and submits full
 * RandomX blocks. Coinbase carries the HOBC telemetry marker with algo=2; a
 * per-minute census heartbeat (algo=randomx) is pushed to the node.
 *
 * Byte layout (identical to the node + miner, proofs 11/13):
 *   blob prefix (80B) = version(4 LE) | prev_internal(32) | merkle_internal(32) |
 *                       ntime(4 LE) | nbits(4 LE) | height(4 LE)
 *   miner appends nonce64(8 LE); seed = "HOBCRX01" || (height/2048) LE32.
 *
 * Env config: RPC_HOST RPC_PORT RPC_USER RPC_PASS POOL_ADDRESS STRATUM_PORT
 *   HOBC_MARKER=1 HOBC_POOL_ID HOBC_ALGO=2 HOBC_POOL_SITE HOBC_POOL_NAME
 *   HOBC_CENSUS_TOKEN POLL_MS SHARE_DIVISOR
 */

// HOBC_ALGO must be set before requiring transactions.js (it reads env at load).
if (process.env.HOBC_ALGO === undefined) process.env.HOBC_ALGO = '2';
if (process.env.HOBC_MARKER === undefined) process.env.HOBC_MARKER = '1';

const net = require('net');
const http = require('http');
const fs = require('fs');
const path = require('path');
const bignum = require('bignum');

const util = require('./lib/util.js');
const BlockTemplate = require('./lib/blockTemplate.js');
const { createShareLogger } = require('./lib/sharelog.js');

// Bitcoin diff-1 target (compact 0x1d00ffff). HOBC uses the same nBits compact
// format for all algos, so share/network difficulty = DIFF1 / target — identical
// convention to ckpool sdiff, keeping CPU stats comparable to SHA/GPU pools.
const DIFF1_TARGET = util.bignumFromBitsHex('1d00ffff');
// Floating-point difficulty = DIFF1_TARGET / target. bignum .div() is INTEGER division,
// so DIFF1_TARGET.div(target) floors to 0 whenever target > DIFF1_TARGET (i.e. any
// difficulty < 1 -- the normal case for CPU RandomX share targets). Scale the numerator
// by 2^64 before the integer divide to retain fractional precision, then divide back out
// in floating point. Used for both assigned share diff and actual submitted-share diff so
// they appear correctly in the sharelog / pool page / census hashrate.
const DIFF_SCALE_BN = require('bignum')('18446744073709551616'); // 2^64
const DIFF_SCALE_F = Math.pow(2, 64);
function diffFromTarget(targetBn) {
    try {
        return Number(DIFF1_TARGET.mul(DIFF_SCALE_BN).div(targetBn).toString()) / DIFF_SCALE_F;
    } catch (e) {
        return 0;
    }
}

const CFG = {
    rpcHost: process.env.RPC_HOST || '127.0.0.1',
    rpcPort: parseInt(process.env.RPC_PORT || '19889', 10),
    rpcUser: process.env.RPC_USER || 'rt',
    rpcPass: process.env.RPC_PASS || 'rtpass123',
    poolAddress: process.env.POOL_ADDRESS || '',
    stratumPort: parseInt(process.env.STRATUM_PORT || '5559', 10),
    pollMs: parseInt(process.env.POLL_MS || '1000', 10),
    shareDivisor: parseFloat(process.env.SHARE_DIVISOR || '1'), // legacy: share_target = block_target/divisor (>1 => harder)
    shareDiff: parseFloat(process.env.SHARE_DIFF || '0.000002'), // fixed share difficulty (ckpool diff-1 units); fractional so a single CPU submits frequent shares
    poolId: parseInt(process.env.HOBC_POOL_ID || '0', 10) & 0xff,
    poolName: process.env.HOBC_POOL_NAME || 'HOBC CPU (RandomX)',
    poolSite: process.env.HOBC_POOL_SITE || '',
    censusToken: process.env.HOBC_CENSUS_TOKEN || '',
    censusMs: parseInt(process.env.HOBC_CENSUS_INTERVAL_MS || '60000', 10),
    logDir: process.env.LOG_DIR || path.join(__dirname, 'logs'),
    // Public-stats sharelog/pool.status tree consumed by pool_stats_collector.py (pool id 'cpu').
    statsLogDir: process.env.STATS_LOG_DIR || '/home/hobbyhashcoin/hobbyhash-logs/hobc-cpu',
    blocksStateFile: process.env.BLOCKS_STATE_FILE || '/home/hobbyhashcoin/hobbyhash-data/mainnet/payoutd-cpu-state.json',
};

if (!CFG.poolAddress) { console.error('FATAL: POOL_ADDRESS required'); process.exit(1); }

function log(sev, msg) {
    const line = `[${new Date().toISOString()}] ${sev} ${msg}`;
    console.log(line);
}
function shareLog(obj) {
    try {
        fs.appendFileSync(path.join(CFG.logDir, 'shares.log'), JSON.stringify(obj) + '\n');
    } catch (e) { /* non-fatal */ }
}

/* ---------- minimal JSON-RPC ---------- */
function rpc(method, params) {
    return new Promise((resolve, reject) => {
        const body = JSON.stringify({ jsonrpc: '1.0', id: 'cpu', method, params: params || [] });
        const req = http.request({
            host: CFG.rpcHost, port: CFG.rpcPort, method: 'POST', path: '/',
            headers: {
                'Content-Type': 'text/plain',
                'Content-Length': Buffer.byteLength(body),
                'Authorization': 'Basic ' + Buffer.from(CFG.rpcUser + ':' + CFG.rpcPass).toString('base64'),
            },
        }, (res) => {
            let data = '';
            res.on('data', (c) => data += c);
            res.on('end', () => {
                try {
                    const j = JSON.parse(data);
                    if (j.error) return reject(new Error(JSON.stringify(j.error)));
                    resolve(j.result);
                } catch (e) { reject(new Error('bad rpc response: ' + data.slice(0, 200))); }
            });
        });
        req.on('error', reject);
        req.write(body); req.end();
    });
}

/* ---------- state ---------- */
let currentJob = null;      // { template, jobId, blobPrefixHex, seedHex, shareTargetHex, shareTargetInt, blockTargetInt, height }
let jobCounter = 0;
const clients = new Map();  // id -> { socket, address, worker, authorized }
let clientCounter = 0;
let lastPrevHash = null;
let minNextHeight = 0;   // don't serve a job until GBT height reaches this (block connection pacing)

// Public-stats writer (ckpool-compatible sharelog + pool.status + block state).
const shareLogger = createShareLogger({
    shareLogDir: CFG.statsLogDir,
    stratumPort: CFG.stratumPort,
    blocksStateFile: CFG.blocksStateFile,
});

// census accumulators
let acceptedShares = 0;
let acceptedShareDiffSum = 0;
let blocksFound = 0;

function seedKeyHex(height) {
    const epoch = Math.floor(height / 2048);
    const buf = Buffer.alloc(12);
    buf.write('HOBCRX01', 0, 8, 'ascii');
    buf.writeUInt32LE(epoch >>> 0, 8);
    return buf.toString('hex');
}

// 32-byte little-endian hex of a bignum target
function targetLEHex(targetBn) {
    let be = targetBn.toBuffer({ endian: 'big', size: 32 });
    return util.reverseBuffer(be).toString('hex');
}

function buildBlobPrefix(tmpl, merkleRootHex) {
    const d = tmpl.rpcData;
    const version = Buffer.alloc(4); version.writeUInt32LE(d.version >>> 0, 0);
    const prev = util.reverseBuffer(Buffer.from(d.previousblockhash, 'hex')); // internal
    const merkle = Buffer.from(merkleRootHex || tmpl.merkleRoot, 'hex');      // internal
    const ntime = Buffer.alloc(4); ntime.writeUInt32LE(d.curtime >>> 0, 0);
    const nbits = Buffer.alloc(4); nbits.writeUInt32LE(parseInt(d.bits, 16) >>> 0, 0);
    const height = Buffer.alloc(4); height.writeUInt32LE(d.height >>> 0, 0);
    return Buffer.concat([version, prev, merkle, ntime, nbits, height]).toString('hex'); // 80B
}

function minerBindForClient(tmpl, client) {
    return tmpl.bindMiner(client.worker || '', client.userAgent || '');
}

async function refreshTemplate(force) {
    let gbt;
    try {
        gbt = await rpc('getblocktemplate', [{ rules: ['segwit', 'randomx'] }]);
    } catch (e) {
        log('error', 'getblocktemplate failed: ' + e.message);
        return;
    }
    if (!force && lastPrevHash === gbt.previousblockhash && currentJob) return;
    const isNewBlock = lastPrevHash !== gbt.previousblockhash;
    lastPrevHash = gbt.previousblockhash;

    if (String(gbt.powalgo || '').toLowerCase() !== 'randomx') {
        // Below the race activation height the node won't honor the randomx rule; nothing to mine.
        if (currentJob) currentJob = null;
        return;
    }

    // Block-connection pacing: after we submit a block we bump minNextHeight; wait until the
    // node's template reflects the connected block before serving the next job. Prevents building
    // block N+1 on a parent the node hasn't made the active tip yet.
    if (gbt.height < minNextHeight) {
        setTimeout(() => refreshTemplate(true), 100);
        return;
    }

    const jobId = (++jobCounter).toString(16);
    const tmpl = new BlockTemplate(jobId, gbt, gbt.coinbasevalue, [], CFG.poolAddress);
    const blockTargetInt = util.bignumFromBitsHex(gbt.bits);
    // Share target. RandomX block difficulty sits at the diff-1 floor (~2^32 hashes per
    // block share), which a single CPU can take days to hit, so a fixed fractional share
    // difficulty (SHARE_DIFF, in ckpool diff-1 units) is used to accept frequent easy
    // shares: share_target = DIFF1_TARGET / shareDiff (shareDiff < 1 => easier than a
    // block). Block detection is independent (getrandomxhash.meets_bits), so easy shares
    // never gate block submission. Legacy SHARE_DIVISOR (>1 => harder) still applies when
    // SHARE_DIFF is unset/<=0.
    let shareTargetInt;
    if (isFinite(CFG.shareDiff) && CFG.shareDiff > 0) {
        shareTargetInt = CFG.shareDiff < 1
            ? DIFF1_TARGET.mul(Math.max(1, Math.round(1 / CFG.shareDiff)))
            : DIFF1_TARGET.div(Math.floor(CFG.shareDiff));
    } else {
        shareTargetInt = CFG.shareDivisor > 1 ? blockTargetInt.div(Math.floor(CFG.shareDivisor)) : blockTargetInt;
    }
    // A hash meeting the (harder) block target is always a valid share; never let the
    // share target be harder than the block target.
    if (shareTargetInt.lt(blockTargetInt)) shareTargetInt = blockTargetInt;

    currentJob = {
        template: tmpl,
        jobId,
        blobPrefixHex: buildBlobPrefix(tmpl),
        seedHex: seedKeyHex(gbt.height),
        shareTargetHex: targetLEHex(shareTargetInt),
        shareTargetInt,
        blockTargetInt,
        height: gbt.height,
    };
    log('info', `new template job=${jobId} height=${gbt.height} prev=${gbt.previousblockhash.slice(0, 16)} clean=${isNewBlock}`);
    broadcastJob(isNewBlock);
}

function jobMessage(clean, client) {
    let blobPrefix = currentJob.blobPrefixHex;
    if (client && currentJob.template && typeof currentJob.template.bindMiner === 'function') {
        const bind = minerBindForClient(currentJob.template, client);
        blobPrefix = buildBlobPrefix(currentJob.template, bind.merkleRoot);
    }
    return JSON.stringify({
        method: 'job',
        params: {
            job_id: currentJob.jobId,
            blob_prefix: blobPrefix,
            seed_key: currentJob.seedHex,
            target: currentJob.shareTargetHex,
            height: currentJob.height,
            clean: !!clean,
        },
    }) + '\n';
}

function broadcastJob(clean) {
    if (!currentJob) return;
    for (const c of clients.values()) {
        if (c.authorized) {
            try { c.socket.write(jobMessage(clean, c)); } catch (e) {}
        }
    }
}

async function handleSubmit(client, id, params) {
    const reply = (obj) => { try { client.socket.write(JSON.stringify({ id, result: obj }) + '\n'); } catch (e) {} };
    // Snapshot the job at entry: currentJob may be replaced across the await below
    // (e.g. after a block is found). Verifying and serializing must use the SAME job.
    const job = currentJob;
    if (!job || params.job_id !== job.jobId) {
        return reply({ ok: false, error: 'stale or unknown job' });
    }
    const nonce = String(params.nonce64 || '');
    const submittedHash = String(params.hash || '').toLowerCase();
    if (!/^[0-9]+$/.test(nonce) || !/^[0-9a-f]{64}$/.test(submittedHash)) {
        return reply({ ok: false, error: 'bad submit params' });
    }
    const d = job.template.rpcData;
    const minerBind = minerBindForClient(job.template, client);

    // Verify with the node consensus hasher. merkleroot passed as display hex = reverse(internal).
    const merkleDisplay = util.reverseBuffer(Buffer.from(minerBind.merkleRoot, 'hex')).toString('hex');
    let res;
    try {
        res = await rpc('getrandomxhash', [d.version, d.previousblockhash, merkleDisplay, d.curtime, d.bits, d.height, nonce]);
    } catch (e) {
        return reply({ ok: false, error: 'verify error: ' + e.message });
    }
    if (res.hash !== submittedHash) {
        shareLog({ t: Date.now(), worker: client.worker, addr: client.address, nonce, ok: false, reason: 'hash-mismatch' });
        shareLogger.writeShare({ worker: client.worker, ip: client.ip, shareDigest: submittedHash }, false);
        return reply({ ok: false, error: 'hash mismatch' });
    }
    const hashInt = bignum(submittedHash, 16);
    if (hashInt.gt(job.shareTargetInt)) {
        shareLog({ t: Date.now(), worker: client.worker, addr: client.address, nonce, ok: false, reason: 'above-share-target' });
        shareLogger.writeShare({ worker: client.worker, ip: client.ip, shareDigest: submittedHash }, false);
        return reply({ ok: false, error: 'above share target' });
    }

    // Accepted share. Difficulty = DIFF1 / target (ckpool sdiff convention), computed in
    // floating point so sub-1 difficulties are preserved (bignum integer div floors to 0).
    const shareDiffVal = diffFromTarget(hashInt);
    const assignedDiffVal = diffFromTarget(job.shareTargetInt);
    acceptedShares++;
    // Accumulate the pool-ASSIGNED share difficulty (not the actual submitted-hash
    // difficulty). Summing assigned diff is the standard, low-variance, unbiased
    // hashrate estimator: hashrate ~= sum(assigned_diff) * 2^32 / interval. Summing the
    // actual submitted diff is dominated by rare high-diff shares and grossly overstates
    // a CPU's real hashrate. shareDiffVal is still recorded in the sharelog below.
    acceptedShareDiffSum += (isFinite(assignedDiffVal) && assignedDiffVal > 0) ? assignedDiffVal : 0;
    shareLog({ t: Date.now(), worker: client.worker, addr: client.address, nonce, ok: true, block: !!res.meets_bits, hash: submittedHash });
    shareLogger.writeShare({
        worker: client.worker,
        ip: client.ip,
        difficulty: assignedDiffVal,
        shareDiff: shareDiffVal,
        shareDigest: submittedHash,
    }, true);

    let isBlock = false;
    if (res.meets_bits && !job.blockSubmitted) {
        // First block-quality share for this job wins; mark so concurrent shares don't
        // re-submit stale duplicates for the same (now solved) template.
        job.blockSubmitted = true;
        // Build + submit the full RandomX block. mixHash = internal digest = reverse(display hash).
        const mixInternal = util.reverseBuffer(Buffer.from(submittedHash, 'hex')).toString('hex');
        const nonceHex = BigInt(nonce).toString(16);
        const blockHex = job.template.serializeBlock(
            submittedHash, nonceHex, mixInternal, minerBind.genTx, minerBind.merkleRoot
        ).toString('hex');
        try {
            const sub = await rpc('submitblock', [blockHex]);
            if (sub === null) {
                isBlock = true; blocksFound++;
                shareLogger.recordBlock({ worker: client.worker, height: job.height, blockHash: submittedHash });
                log('special', `*** RANDOMX BLOCK ACCEPTED height=${job.height} by ${client.worker} nonce=${nonce} ***`);
                // Canonical solve line consumed by payoutd (SOLVE_RE). Winner = payout
                // address (before first dot), matching the GPU pool convention so the
                // CPU payout daemon auto-detects and pays RandomX block solvers.
                log('special', `Solved and confirmed block ${job.height} by ${client.address}.${client.worker}`);
                // Pace block production: stop serving/accepting shares until the node has
                // actually connected this block (tip advances). Building block N+1 before
                // N is the active tip yields a block on an unconnected parent (rejected).
                currentJob = null;
                minNextHeight = job.height + 1;
                setTimeout(() => refreshTemplate(true), 100);
            } else {
                log('error', 'submitblock rejected: ' + JSON.stringify(sub));
                job.blockSubmitted = false; // allow another share to try
            }
        } catch (e) {
            log('error', 'submitblock error: ' + e.message);
            job.blockSubmitted = false;
        }
    }
    reply({ ok: true, block: isBlock });
}

/* ---------- stratum server ---------- */
const server = net.createServer((socket) => {
    const cid = ++clientCounter;
    const client = { socket, address: '', worker: 'rxminer', userAgent: '', authorized: false, ip: socket.remoteAddress };
    clients.set(cid, client);
    socket.setEncoding('utf8');
    let buf = '';
    socket.on('data', (chunk) => {
        buf += chunk;
        let idx;
        while ((idx = buf.indexOf('\n')) !== -1) {
            const line = buf.slice(0, idx); buf = buf.slice(idx + 1);
            if (!line.trim()) continue;
            let msg;
            try { msg = JSON.parse(line); } catch (e) { continue; }
            const method = msg.method;
            if (method === 'subscribe') {
                socket.write(JSON.stringify({ id: msg.id, result: { ok: true } }) + '\n');
            } else if (method === 'authorize') {
                const p = msg.params || {};
                client.address = String(p.address || '');
                client.worker = String(p.worker || 'rxminer');
                if (p.agent || p.user_agent || p.userAgent)
                    client.userAgent = String(p.agent || p.user_agent || p.userAgent || '');
                client.authorized = !!client.address;
                socket.write(JSON.stringify({ id: msg.id, result: { ok: client.authorized } }) + '\n');
                if (client.authorized && currentJob) socket.write(jobMessage(true, client));
                log('info', `auth ${client.worker}@${client.ip} address=${client.address} ok=${client.authorized}`);
            } else if (method === 'submit') {
                handleSubmit(client, msg.id, msg.params || {});
            }
        }
    });
    socket.on('error', () => {});
    socket.on('close', () => { clients.delete(cid); });
});

/* ---------- census heartbeat ---------- */
async function sendCensus() {
    if (!CFG.censusToken) return;
    const now = Date.now();
    // hashrate estimate: accepted-share difficulty over the interval * 2^0 ... derive from share target.
    // shares * (network_difficulty scaled) is complex; use accepted share diff sum * 2^32 / interval as a coarse H/s.
    const workers = new Set(); const miners = new Set();
    for (const c of clients.values()) { if (c.authorized) { workers.add(c.worker + '@' + c.address); miners.add(c.address); } }
    // Coarse hashrate from accepted shares against the share target difficulty.
    let shareDiff = 1;
    try { shareDiff = util.kawpowNetworkDifficultyFromTargetHex ? 1 : 1; } catch (e) {}
    const hashps = (acceptedShareDiffSum * Math.pow(2, 32)) / (CFG.censusMs / 1000);
    acceptedShareDiffSum = 0;

    // Field names MUST match the node submitcensus parser (ParseAlgoReport in
    // src/rpc/telemetry.cpp): hashrate / unique_miners / workers. Using hashps/miners
    // here silently drops the CPU pool's hashrate and miner count from getnetworkstats.
    const report = {
        pool_name: CFG.poolName,
        pool_site: CFG.poolSite,
        randomx: { hashrate: hashps, unique_miners: miners.size, workers: workers.size },
        sha256: { hashrate: 0, unique_miners: 0, workers: 0 },
        kawpow: { hashrate: 0, unique_miners: 0, workers: 0 },
    };
    try {
        await rpc('submitcensus', [CFG.censusToken, CFG.poolId, report]);
    } catch (e) {
        log('warning', 'submitcensus failed: ' + e.message);
    }
}

/* ---------- boot ---------- */
server.listen(CFG.stratumPort, () => {
    log('info', `HOBC CPU RandomX pool listening on :${CFG.stratumPort} (pool_id=${CFG.poolId}, algo=2, marker=${process.env.HOBC_MARKER})`);
});
refreshTemplate(true);
setInterval(() => refreshTemplate(false), CFG.pollMs);
if (CFG.censusToken) setInterval(sendCensus, CFG.censusMs);
