var bignum = require('bignum');
var crypto = require('crypto');

var merkle = require('./merkleTree.js');
var transactions = require('./transactions.js');
var util = require('./util.js');
var hobcMarker = require('./hobc_marker.js');

    
/**
 * The BlockTemplate class holds a single job.
 * and provides several methods to validate and submit it to the daemon coin
**/
var BlockTemplate = module.exports = function BlockTemplate(jobId, rpcData, reward, recipients, poolAddress){

    //epoch length
    const EPOCH_LENGTH = 7500;
    
    //private members
    var submits = [];
    var minerBindCache = {};

    //public members
    this.rpcData = rpcData;
    this.jobId = jobId;

    // get target info
    this.target = bignum(rpcData.target, 16);
    this.target_hex = rpcData.target;

    this.difficulty = util.kawpowNetworkDifficultyFromTargetHex(this.target_hex);
    // Hot path quiet: difficulty is available on the job object when needed.

    //nTime
    var nTime = util.packUInt32BE(rpcData.curtime).toString('hex');

    //current time of issuing the template
    var curTime = Date.now() / 1000 | 0;

    // generate the fees and coinbase tx
    var blockReward = this.rpcData.coinbasevalue;
 
    var fees = [];
    rpcData.transactions.forEach(function(value) {
        fees.push(value);
    });
    this.rewardFees = transactions.getFees(fees);
    rpcData.rewardFees = this.rewardFees;

    if (typeof this.genTx === 'undefined') {
        this.genTx = transactions.createGeneration(rpcData, blockReward, this.rewardFees, recipients, poolAddress).toString('hex');
        this.genTxHash = transactions.txHash();
        
        // console.log('this.genTxHash: ' + transactions.txHash());
        // console.log('this.merkleRoot: ' + merkle.getRoot(rpcData, this.genTxHash));
    }

    // generate the merkle root
    this.prevHashReversed = util.reverseBuffer(new Buffer(rpcData.previousblockhash, 'hex')).toString('hex');
    this.merkleRoot = merkle.getRoot(rpcData, this.genTxHash);
    this.txCount = this.rpcData.transactions.length + 1; // add total txs and new coinbase
    this.merkleRootReversed = util.reverseBuffer(new Buffer(this.merkleRoot, 'hex')).toString('hex');
    // we can't do anything else until we have a submission

    // console.log('this.prevHashReversed: ' + this.prevHashReversed);

    this.serializeGpuHeader = function (nonceHex, mixhashHex, merkleRootHex) {
        var header = Buffer.alloc(116);
        var position = 0;
        var merkleUse = merkleRootHex || this.merkleRoot;

        header.writeUInt32LE(this.rpcData.version >>> 0, position);
        position += 4;
        util.reverseBuffer(new Buffer(this.rpcData.previousblockhash, 'hex')).copy(header, position);
        position += 32;
        Buffer.from(merkleUse, 'hex').copy(header, position);
        position += 32;
        header.writeUInt32LE(this.rpcData.curtime >>> 0, position);
        position += 4;
        header.writeUInt32LE(parseInt(this.rpcData.bits, 16) >>> 0, position);
        position += 4;

        var nonceStr = String(nonceHex || '0').replace(/^0x/i, '');
        while (nonceStr.length < 16) {
            nonceStr = '0' + nonceStr;
        }
        header.writeBigUInt64LE(BigInt('0x' + nonceStr.slice(-16)), position);
        position += 8;

        var mixStr = String(mixhashHex || '0').replace(/^0x/i, '');
        while (mixStr.length < 64) {
            mixStr = '0' + mixStr;
        }
        Buffer.from(mixStr.slice(-64), 'hex').copy(header, position);

        return header;
    };

    /* Per-client coinbase identity (rig/worker). Merkle changes with the coinbase. */
    this.bindMiner = function (workerName, userAgent) {
        var worker = hobcMarker.workerLabel(workerName);
        var rig = hobcMarker.rigLabel(userAgent);
        var cacheKey = String(worker || '') + '\0' + String(rig || '');
        if (minerBindCache[cacheKey])
            return minerBindCache[cacheKey];

        var genTx = transactions.createGeneration(
            rpcData, blockReward, this.rewardFees, recipients, poolAddress,
            { rig: rig, worker: worker }
        );
        genTx = String(genTx);
        var genTxHash = transactions.txHash();
        var merkleRoot = merkle.getRoot(rpcData, genTxHash);
        var bind = {
            genTx: genTx,
            genTxHash: genTxHash,
            merkleRoot: merkleRoot
        };
        minerBindCache[cacheKey] = bind;
        return bind;
    };

    // Legacy kawpow 80-byte prefix used only for header hash / stratum jobs.
    this.serializeKawPowHeaderPrefix = function () {
        var header = new Buffer(80);
        var position = 0;

        header.write(util.packUInt32BE(this.rpcData.height).toString('hex'), position, 4, 'hex');
        header.write(this.rpcData.bits, position += 4, 4, 'hex');
        header.write(nTime, position += 4, 4, 'hex');
        header.write(this.merkleRoot, position += 4, 32, 'hex');
        header.write(this.rpcData.previousblockhash, position += 32, 32, 'hex');
        header.writeUInt32BE(this.rpcData.version, position + 32, 4);

        return util.reverseBuffer(header);
    };

    this.serializeHeader = function () {
        return this.serializeKawPowHeaderPrefix();
    };

    // join the header and txs together
    this.serializeBlock = function (header_hash, nonce, mixhash, genTxHex, merkleRootHex) {
        var blockHeader = this.serializeGpuHeader(nonce, mixhash, merkleRootHex);
        var coinbaseHex = genTxHex || this.genTx;
        var buf = Buffer.concat([
            blockHeader,
            util.varIntBuffer(this.rpcData.transactions.length + 1),
            new Buffer(coinbaseHex, 'hex')
        ]);

        if (this.rpcData.transactions.length > 0) {
            this.rpcData.transactions.forEach(function (value) {
                buf = Buffer.concat([buf, new Buffer(value.data, 'hex')]);
            });
        }

        return buf;
    };

    // submit header_hash and nonce
    this.registerSubmit = function(header, nonce){
        var submission = header + nonce;
        if (submits.indexOf(submission) === -1){
            submits.push(submission);
            return true;
        }
        return false;
    };


    var target = util.kawpowShareTargetHex(Math.max(this.difficulty, 0.000000001));

    this.epoch_number = Math.floor(this.rpcData.height / EPOCH_LENGTH);
    var seedhash = util.kawPowSeedHashHex(this.rpcData.height);
    var header_hash = util.kawPowHeaderHashHex(this.rpcData, this.merkleRoot);

    //change override_target to a minimum wanted target. This is useful for e.g. testing on testnet.
    var override_target = 0;
    //override_target = 0x0000000FFFFF0000000000000000000000000000000000000000000000000000;
	if (override_target != 0) {
        var overrideHex = override_target.toString(16);
        while (overrideHex.length < 64) {
            overrideHex = '0' + overrideHex;
        }
        if (bignum(target, 16).gt(bignum(overrideHex, 16))) {
            target = overrideHex.substr(0, 64);
        }
    }
    
    // used for mining.notify
    this.getJobParams = function(){
        // console.log("RPC DATA IN job params: "+JSON.stringify(this.rpcData));
        if (!this.jobParams){
            this.jobParams = [
                this.jobId,
                header_hash,
                seedhash,
                target,  //target is overridden later to match miner varDiff
                true,
                this.rpcData.height,
                this.rpcData.bits
            ];
        }
        return this.jobParams;
    };
};

