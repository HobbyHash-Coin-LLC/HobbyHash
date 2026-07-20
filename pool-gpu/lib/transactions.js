var bitcoin = require('bitcoinjs-lib');
var util = require('./util.js');
var hobcMarker = require('./hobc_marker.js');

// HOBC V6 telemetry marker config (env-driven; harmless below the race activation height,
// consensus-required at/after it). algo defaults to 1 (kawpow) for the GPU pool.
var HOBC_MARKER = String(process.env.HOBC_MARKER || '') === '1';
var HOBC_POOL_ID = parseInt(process.env.HOBC_POOL_ID || '0', 10) & 0xff;
var HOBC_ALGO = (process.env.HOBC_ALGO !== undefined) ? (parseInt(process.env.HOBC_ALGO, 10) & 0xff) : 1;
var HOBC_POOL_SITE = String(process.env.HOBC_POOL_SITE || '');

// public members
var txHash;

exports.txHash = function(){
  return txHash;
};

function scriptCompile(addrHash){
    script = bitcoin.script.compile(
        [
            bitcoin.opcodes.OP_DUP,
            bitcoin.opcodes.OP_HASH160,
            addrHash,
            bitcoin.opcodes.OP_EQUALVERIFY,
            bitcoin.opcodes.OP_CHECKSIG
        ]);
    return script;
}

function scriptFoundersCompile(address){
    script = bitcoin.script.compile(
        [
            bitcoin.opcodes.OP_HASH160,
            address,
            bitcoin.opcodes.OP_EQUAL
        ]);
    return script;
}


function buildCoinbaseScriptSig(rpcData, identity) {
    var height = rpcData.height;
    var chunks = [];
    var id = identity || {};
    chunks.push(bitcoin.script.number.encode(height));
    // HOBC regtest/mainnet templates always append OP_0 dummy extranonce (see node/miner.cpp).
    chunks.push(bitcoin.opcodes.OP_0);
    if (rpcData.coinbaseaux && typeof rpcData.coinbaseaux === 'object') {
        Object.keys(rpcData.coinbaseaux).forEach(function (key) {
            chunks.push(Buffer.from(rpcData.coinbaseaux[key], 'hex'));
        });
    }
    // HOBC V6: push the telemetry marker as a data chunk. bitcoin.script.compile adds the
    // correct pushdata prefix; the node parser locates the "HOBC" magic by scanning scriptSig.
    // winning_share_diff = network difficulty at job time (from GBT bits). Fixed-size rig/worker
    // TLVs are stamped per-client so the solving worker is on-chain in the coinbase.
    if (HOBC_MARKER) {
        var netDiff = hobcMarker.bitsToDifficulty(rpcData.bits);
        chunks.push(hobcMarker.buildMarker(
            HOBC_POOL_ID, HOBC_ALGO, netDiff, HOBC_POOL_SITE,
            id.rig || '', id.worker || ''
        ));
    }
    return bitcoin.script.compile(chunks);
}


exports.createGeneration = function(rpcData, blockReward, feeReward, recipients, poolAddress, identity){
    var _this = this;
    var blockPollingIntervalId;

    var emitLog = function (text) {
        _this.emit('log', 'debug', text);
    };
    var emitWarningLog = function (text) {
        _this.emit('log', 'warning', text);
    };
    var emitErrorLog = function (text) {
        _this.emit('log', 'error', text);
    };
    var emitSpecialLog = function (text) {
        _this.emit('log', 'special', text);
    };

    var poolAddrHash = bitcoin.address.fromBase58Check(poolAddress).hash;

    var tx = new bitcoin.Transaction();
    tx.version = 2;
    var blockHeight = rpcData.height;
    tx.locktime = Math.max(0, blockHeight - 1);

    tx.addInput(new Buffer('0000000000000000000000000000000000000000000000000000000000000000', 'hex'),
        0xFFFFFFFF,
        0xFFFFFFFF,
        buildCoinbaseScriptSig(rpcData, identity)
    );

    // calculate total fees
    var feePercent = 0;
    for (var i = 0; i < recipients.length; i++) {
        feePercent = feePercent + recipients[i].percent;
    }

    tx.addOutput(
        scriptCompile(poolAddrHash),
        Math.floor(blockReward * (1 - (feePercent / 100)))
    );


    for (var i = 0; i < recipients.length; i++) {
       tx.addOutput(
           scriptCompile(bitcoin.address.fromBase58Check(recipients[i].address).hash),
           Math.round((blockReward) * (recipients[i].percent / 100))
       );
    }


    if (rpcData.default_witness_commitment !== undefined) {
        tx.addOutput(new Buffer(rpcData.default_witness_commitment, 'hex'), 0);
    }

    if (rpcData.default_witness_commitment !== undefined) {
        tx.ins[0].witness = [Buffer.alloc(32, 0)];
    }

    txHex = tx.toHex(undefined, { includeWitness: true });

    // this txHash is used elsewhere. Don't remove it.
    txHash = tx.getHash().toString('hex');

    return txHex;
};

module.exports.getFees = function(feeArray){
    var fee = Number();
    feeArray.forEach(function(value) {
        fee = fee + Number(value.fee);
    });
    return fee;
};
