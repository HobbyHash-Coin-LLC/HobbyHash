#!/usr/bin/env node
/**
 * HOBC V5 mainnet GPU stratum (KPSS) on :5558.
 * Idles during SHA256 windows; mines KawPow during GPU windows after H8000.
 */
const fs = require('fs');
const path = require('path');
const http = require('http');

const root = path.join(__dirname, '..', '..');
const mainConf = path.join('/home/hobbyhashcoin', 'hobbyhash-conf', 'hobbyhash-mainnet.conf');
const cookieFile = path.join('/home/hobbyhashcoin', 'hobbyhash-data', 'mainnet', '.cookie');
const poolAddressFile = path.join(root, 'hobbyhash-conf', 'mainnet-gpu-pool.address');
const RPC_PORT = 18762;

function readRpcAuth() {
  if (fs.existsSync(cookieFile)) {
    const cookie = fs.readFileSync(cookieFile, 'utf8').trim();
    const parts = cookie.split(':');
    return { user: parts[0], password: parts.slice(1).join(':') };
  }
  const conf = fs.readFileSync(mainConf, 'utf8');
  const userMatch = conf.match(/^rpcuser=(.+)$/m);
  const passMatch = conf.match(/^rpcpassword=(.+)$/m);
  if (!userMatch || !passMatch) {
    console.error('Could not read rpcuser/rpcpassword from', mainConf);
    process.exit(1);
  }
  return { user: userMatch[1].trim(), password: passMatch[1].trim() };
}

if (!fs.existsSync(poolAddressFile)) {
  console.error('GPU pool coinbase address missing — run mainnet-gpu-pool-node.sh first:', poolAddressFile);
  process.exit(1);
}

const { user: rpcUser, password: rpcPass } = readRpcAuth();
const poolAddress = fs.readFileSync(poolAddressFile, 'utf8').trim();

const Stratum = require('./lib');

// Ask the local node whether a payout address is valid. Uses the same check the
// payout daemon runs at payout time, so the stratum never authorizes a worker
// whose username is an address the daemon would later reject (e.g. a bech32
// typo with the digit "1" in place of the letter "l"). Fails open on RPC errors
// so a transient node hiccup can never block legitimate miners.
function validateAddressRpc(address, cb) {
  const body = JSON.stringify({ jsonrpc: '1.0', id: 'kpss-auth', method: 'validateaddress', params: [address] });
  const req = http.request({
    host: '127.0.0.1',
    port: RPC_PORT,
    method: 'POST',
    auth: `${rpcUser}:${rpcPass}`,
    headers: { 'Content-Type': 'text/plain', 'Content-Length': Buffer.byteLength(body) },
    timeout: 4000,
  }, (res) => {
    let data = '';
    res.on('data', (chunk) => { data += chunk; });
    res.on('end', () => {
      try {
        const parsed = JSON.parse(data);
        cb(parsed.error || null, parsed.result || null);
      } catch (err) {
        cb(err, null);
      }
    });
  });
  req.on('timeout', () => { req.destroy(new Error('validateaddress RPC timeout')); });
  req.on('error', (err) => { cb(err, null); });
  req.write(body);
  req.end();
}

const myCoin = {
  name: 'HobbyHash',
  symbol: 'HOBC',
  algorithm: 'kawpow',
  peerMagic: 'c1a0f1ce',
  peerMagicTestnet: 'fcb1c3b5',
  peerMagicRegtest: 'dab5c3f1',
};

const STRATUM_PORT = 5558;
const VALIDATOR_PORT = Number(process.env.HOBC_KAWPOWD_PORT || 8889);
const SHARE_LOG_DIR = '/home/hobbyhashcoin/hobbyhash-logs/kpss-gpu';
const BLOCKS_STATE_FILE = '/home/hobbyhashcoin/hobbyhash-data/mainnet/payoutd-gpu-state.json';

const pool = Stratum.createPool({
  coin: myCoin,
  address: poolAddress,
  rewardRecipients: {},
  idleDuringSha: true,
  idlePingIntervalSec: 29,
  shareLogDir: SHARE_LOG_DIR,
  blocksStateFile: BLOCKS_STATE_FILE,
  stratumPort: STRATUM_PORT,
  // 5s is enough for solo KawPow; 1s GBT polling was unnecessary localhost RPC churn.
  blockRefreshInterval: 5000,
  getNewBlockAfterFound: true,
  jobRebroadcastTimeout: 55,
  connectionTimeout: 3600,
  emitInvalidBlockHashes: false,
  tcpProxyProtocol: false,
  banning: { enabled: false },
  ports: {
    [STRATUM_PORT]: {
      diff: 0.15,
      varDiff: {
        minDiff: 0.15,
        maxDiff: 512,
        targetTime: 15,
        retargetTime: 90,
        variancePercent: 30,
      },
    },
  },
  daemons: [{
    host: '127.0.0.1',
    port: RPC_PORT,
    user: rpcUser,
    password: rpcPass,
  }],
  p2p: { enabled: false },
  kawpow_validator: 'kawpowd',
  kawpow_wrapper_host: '127.0.0.1',
  kawpow_wrapper_port: VALIDATOR_PORT,
}, (_ip, _port, workerName, password, extraNonce1, version, callback) => {
  const payoutAddress = String(workerName || '').split('.')[0].trim();
  validateAddressRpc(payoutAddress, (rpcErr, info) => {
    if (rpcErr) {
      // Fail open: don't reject miners on a transient RPC issue. The payout
      // daemon re-validates the address at payout time as a backstop.
      console.log(`Authorize ${workerName} @ mainnet GPU pool (address check skipped: ${rpcErr.message || rpcErr})`);
      callback({ error: null, authorized: true, disconnect: false });
      return;
    }
    if (!info || info.isvalid !== true) {
      console.log(`Reject authorize ${workerName} @ mainnet GPU pool — invalid payout address "${payoutAddress}"`);
      callback({
        error: [20, 'Invalid HOBC payout address: set your miner username to a valid HOBC wallet address', null],
        authorized: false,
        disconnect: true,
      });
      return;
    }
    console.log(`Authorize ${workerName} @ mainnet GPU pool (pass=${password || 'x'}) v=${version} en1=${extraNonce1}`);
    callback({ error: null, authorized: true, disconnect: false });
  });
});

// HOBC V6 census: accumulate accepted-share difficulty for a per-minute hashrate estimate.
let hobcShareDiffSum = 0;

pool.on('share', (isValidShare, isValidBlock, data) => {
  if (isValidShare && data && isFinite(Number(data.difficulty))) {
    hobcShareDiffSum += Number(data.difficulty);
  }
  if (isValidBlock) {
    console.log('Block found by GPU miner (mainnet)');
  } else if (isValidShare) {
    console.log('Valid share');
  } else if (data && data.error) {
    console.log('Share rejected:', JSON.stringify(data.error));
  }
});

// HOBC V6: per-minute census heartbeat.
// By default POSTs to the HobbyHash HTTPS census hub (no operator token/URL setup).
// Optional local-node submitcensus when HOBC_CENSUS_TOKEN is set. Mining RPC stays local.
const https = require('https');
const HOBC_CENSUS_TOKEN = String(process.env.HOBC_CENSUS_TOKEN || '');
const HOBC_POOL_ID = parseInt(process.env.HOBC_POOL_ID || '0', 10) & 0xff;
const HOBC_POOL_SITE = String(process.env.HOBC_POOL_SITE || '');
const HOBC_CENSUS_INTERVAL_MS = 60000;
const HOBC_CENSUS_HUB = String(process.env.HOBC_CENSUS_HUB || '1').trim() !== '0';
const HOBC_CENSUS_URL = String(process.env.HOBC_CENSUS_URL || 'https://hobbyhashcoin.com/api/network/census/submit/').trim() || 'https://hobbyhashcoin.com/api/network/census/submit/';

function submitCensusLocal(reportObj) {
  if (!HOBC_CENSUS_TOKEN) return;
  const body = JSON.stringify({
    jsonrpc: '1.0', id: 'kpss-census', method: 'submitcensus',
    params: [HOBC_CENSUS_TOKEN, HOBC_POOL_ID, reportObj],
  });
  const req = http.request({
    host: '127.0.0.1', port: RPC_PORT, method: 'POST',
    auth: `${rpcUser}:${rpcPass}`,
    headers: { 'Content-Type': 'text/plain', 'Content-Length': Buffer.byteLength(body) },
    timeout: 4000,
  }, (res) => { res.on('data', () => {}); res.on('end', () => {}); });
  req.on('timeout', () => { req.destroy(new Error('submitcensus RPC timeout')); });
  req.on('error', (err) => { console.log('HOBC census local error:', err.message || err); });
  req.write(body);
  req.end();
}

function submitCensusHub(reportObj) {
  if (!HOBC_CENSUS_HUB) return;
  let u;
  try { u = new URL(HOBC_CENSUS_URL); } catch (e) { console.log('HOBC census hub: bad URL'); return; }
  if (u.protocol !== 'https:') { console.log('HOBC census hub: https required'); return; }
  const body = JSON.stringify({
    jsonrpc: '1.0', id: 'kpss-census-hub', method: 'submitcensus',
    params: ['', HOBC_POOL_ID, reportObj],
  });
  const req = https.request({
    hostname: u.hostname,
    port: u.port || 443,
    path: u.pathname + (u.search || ''),
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) },
    timeout: 4000,
  }, (res) => { res.on('data', () => {}); res.on('end', () => {}); });
  req.on('timeout', () => { req.destroy(new Error('submitcensus hub timeout')); });
  req.on('error', (err) => { console.log('HOBC census hub error:', err.message || err); });
  req.write(body);
  req.end();
}

function emitHobcCensus() {
  const hashrate = (hobcShareDiffSum * Math.pow(2, 32)) / (HOBC_CENSUS_INTERVAL_MS / 1000);
  hobcShareDiffSum = 0;
  let workers = 0;
  const miners = new Set();
  try {
    const clients = pool.stratumServer ? pool.stratumServer.getStratumClients() : {};
    for (const id in clients) {
      const c = clients[id];
      if (c && c.authorized === true) {
        workers++;
        miners.add(String(c.workerName || '').split('.')[0].trim());
      }
    }
  } catch (e) { /* fail open */ }
  const report = {
    pool_name: process.env.HOBC_POOL_NAME || 'HobbyHash GPU (KPSS)',
    pool_site: HOBC_POOL_SITE,
    kawpow: { hashrate, unique_miners: miners.size, workers },
  };
  submitCensusHub(report);
  submitCensusLocal(report);
}

setInterval(emitHobcCensus, HOBC_CENSUS_INTERVAL_MS).unref();
console.log(`HOBC census auto-hub=${HOBC_CENSUS_HUB ? 'on' : 'off'} local_token=${HOBC_CENSUS_TOKEN ? 'on' : 'off'} (pool ${HOBC_POOL_ID}, kawpow, every ${HOBC_CENSUS_INTERVAL_MS / 1000}s)`);

pool.on('log', (severity, logKey, logText) => {
  console.log(`${severity}: [${logKey}] ${logText}`);
});

console.log(`Starting HOBC V5 mainnet GPU stratum on 0.0.0.0:${STRATUM_PORT} (RPC ${RPC_PORT}) pool=${poolAddress} kawpowd=${VALIDATOR_PORT}`);
pool.start();
