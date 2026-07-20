/**
 * ckpool-compatible sharelog + pool.status + block state for HOBC public pool
 * stats. Byte-format identical to the KPSS GPU writer (lib/sharelog.js) so the
 * existing pool_stats_collector.py / hub parse CPU shares exactly like SHA/GPU.
 */
var fs = require('fs');
var path = require('path');

function ensureDir(dirPath) {
    try {
        fs.mkdirSync(dirPath, { recursive: true });
    } catch (err) {
        if (err && err.code !== 'EEXIST') {
            console.error('CPU pool stats directory unavailable:', dirPath, err.message || err);
            return false;
        }
    }
    return true;
}

function readJson(filePath, fallback) {
    try {
        if (!fs.existsSync(filePath)) {
            return fallback;
        }
        var raw = fs.readFileSync(filePath, 'utf8');
        if (!raw.trim()) {
            return fallback;
        }
        return JSON.parse(raw);
    } catch (err) {
        return fallback;
    }
}

function writeJsonAtomic(filePath, payload) {
    if (!ensureDir(path.dirname(filePath))) {
        return false;
    }
    var tmp = filePath + '.tmp.' + process.pid;
    try {
        fs.writeFileSync(tmp, JSON.stringify(payload, null, 2) + '\n');
        fs.renameSync(tmp, filePath);
        return true;
    } catch (err) {
        try {
            if (fs.existsSync(tmp)) {
                fs.unlinkSync(tmp);
            }
        } catch (_cleanupErr) {}
        console.error('CPU pool stats write failed:', filePath, err.message || err);
        return false;
    }
}

function normalizeHash(value) {
    if (!value) {
        return '';
    }
    if (Buffer.isBuffer(value)) {
        return value.toString('hex').toLowerCase();
    }
    return String(value).replace(/^0x/i, '').toLowerCase();
}

function createShareLogger(options) {
    var logDir = options.shareLogDir;
    var stratumPort = options.stratumPort || 5559;
    var sharelogPath = path.join(logDir, 'pool', 'hobc-cpu.sharelog');
    var poolStatusPath = path.join(logDir, 'pool', 'pool.status');
    var blocksStatePath = options.blocksStateFile;
    var workinfoidCounter = Date.now();
    var acceptedTotal = 0;
    var rejectedTotal = 0;
    var lastShareTs = 0;

    ensureDir(path.join(logDir, 'pool'));

    if (blocksStatePath && !fs.existsSync(blocksStatePath)) {
        writeJsonAtomic(blocksStatePath, {
            version: 1,
            candidates: [],
            paid: [],
        });
    }

    writePoolStatus();

    function createdateNow() {
        var hr = process.hrtime();
        return String(Math.floor(Date.now() / 1000)) + ',' + String(hr[1] || 0);
    }

    function writePoolStatus() {
        var payload = {
            lastupdate: Math.floor(Date.now() / 1000),
            accepted: acceptedTotal,
            rejected: rejectedTotal,
            lastshare: lastShareTs,
            Users: 0,
        };
        try {
            fs.writeFileSync(poolStatusPath, JSON.stringify(payload) + '\n');
        } catch (err) {
            console.error('CPU pool status write failed:', poolStatusPath, err.message || err);
        }
    }

    function writeShare(shareData, isAccepted) {
        var worker = String(shareData && shareData.worker ? shareData.worker : 'unknown');
        var assignedDiff = Number(shareData && shareData.difficulty ? shareData.difficulty : 0);
        var shareDiff = Number(shareData && shareData.shareDiff ? shareData.shareDiff : assignedDiff);
        if (!isFinite(shareDiff) || shareDiff <= 0) {
            shareDiff = assignedDiff > 0 ? assignedDiff : 0.01;
        }

        var row = {
            workinfoid: workinfoidCounter++,
            diff: assignedDiff,
            sdiff: shareDiff,
            hash: shareData && shareData.shareDigest ? normalizeHash(shareData.shareDigest) : '',
            result: !!isAccepted,
            errn: isAccepted ? 0 : 1,
            createdate: createdateNow(),
            createby: 'code',
            createcode: 'cpu_submit',
            createinet: '0.0.0.0:' + stratumPort,
            workername: worker,
            username: worker.split('.')[0] || worker,
            address: String(shareData && shareData.ip ? shareData.ip : ''),
            agent: 'HOBC/randomx',
        };

        try {
            fs.appendFileSync(sharelogPath, JSON.stringify(row) + '\n');
        } catch (err) {
            console.error('CPU pool sharelog write failed:', sharelogPath, err.message || err);
        }

        if (isAccepted) {
            acceptedTotal += 1;
        } else {
            rejectedTotal += 1;
        }
        lastShareTs = Math.floor(Date.now() / 1000);
        writePoolStatus();
    }

    function recordBlock(shareData) {
        if (!blocksStatePath || !shareData) {
            return;
        }
        var worker = String(shareData.worker || 'unknown');
        var state = readJson(blocksStatePath, { version: 1, candidates: [], paid: [] });
        var candidates = Array.isArray(state.candidates) ? state.candidates : [];
        var height = Number(shareData.height || 0);
        if (height > 0 && candidates.some(function (row) { return Number(row && row.height) === height; })) {
            return;
        }
        var row = {
            height: height > 0 ? height : 'not_available',
            blockhash: normalizeHash(shareData.blockHash || ''),
            workername: worker,
            winner_address: worker.split('.')[0] || worker,
            seen_at: Math.floor(Date.now() / 1000),
            status: 'pending',
            confirmations: 0,
        };
        candidates.unshift(row);
        state.candidates = candidates.slice(0, 200);
        state.last_block_at = row.seen_at;
        writeJsonAtomic(blocksStatePath, state);
    }

    return {
        writeShare: writeShare,
        recordBlock: recordBlock,
    };
}

module.exports = {
    createShareLogger: createShareLogger,
};
