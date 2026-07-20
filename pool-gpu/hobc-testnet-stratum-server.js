#!/usr/bin/env node
/**
 * HOBC V5 burn-in GPU stratum (regtest backend @ H8000, port 15558).
 * Public testnet uses the same activation/GPU seed; local burn-in mines regtest for speed.
 */
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..');
const stateFile = path.join(root, 'hobbyhash-conf', 'regtest-gpu-pool.rpcport');
const cookieFile = path.join(root, 'hobbyhash-data', 'regtest-gpu-pool', 'regtest', '.cookie');
const poolAddressFile = path.join(root, 'hobbyhash-conf', 'regtest-gpu-pool.address');
const rpcPort = Number(fs.readFileSync(stateFile, 'utf8').trim());
const cookie = fs.readFileSync(cookieFile, 'utf8').trim();
const [rpcUser, rpcPass] = cookie.split(':');
const poolAddress = fs.readFileSync(poolAddressFile, 'utf8').trim();

const Stratum = require('./lib');

const myCoin = {
  name: 'HobbyHash Burn-in',
  symbol: 'HOBC',
  algorithm: 'kawpow',
  peerMagic: 'c1a0f1ce',
  peerMagicTestnet: 'fcb1c3b5',
  peerMagicRegtest: 'dab5c3f1',
};

const STRATUM_PORT = 15558;

const pool = Stratum.createPool({
  coin: myCoin,
  address: poolAddress,
  rewardRecipients: {},
  blockRefreshInterval: 1000,
  getNewBlockAfterFound: true,
  jobRebroadcastTimeout: 55,
  connectionTimeout: 3600,
  emitInvalidBlockHashes: false,
  tcpProxyProtocol: false,
  banning: { enabled: false },
  ports: {
    [STRATUM_PORT]: {
      diff: 0.01,
      varDiff: {
        minDiff: 0.001,
        maxDiff: 512,
        targetTime: 15,
        retargetTime: 90,
        variancePercent: 30,
      },
    },
  },
  daemons: [{
    host: '127.0.0.1',
    port: rpcPort,
    user: rpcUser,
    password: rpcPass,
  }],
  p2p: { enabled: false },
  kawpow_validator: 'kawpowd',
  kawpow_wrapper_host: '127.0.0.1',
  kawpow_wrapper_port: 8799,
}, (_ip, _port, workerName, password, extraNonce1, version, callback) => {
  console.log(`Authorize ${workerName} @ burn-in pool (pass=${password || 'x'}) v=${version} en1=${extraNonce1}`);
  callback({ error: null, authorized: true, disconnect: false });
});

pool.on('share', (isValidShare, isValidBlock, data) => {
  if (isValidBlock) {
    console.log('Block found by GPU miner (burn-in)');
  } else if (isValidShare) {
    console.log('Valid share');
  } else {
    console.log('Share rejected');
  }
  if (data) {
    console.log(JSON.stringify(data));
  }
});

pool.on('log', (severity, logKey, logText) => {
  console.log(`${severity}: [${logKey}] ${logText}`);
});

console.log(`Starting HOBC V5 burn-in GPU stratum on 0.0.0.0:${STRATUM_PORT} (regtest RPC ${rpcPort}) pool=${poolAddress}`);
pool.start();
