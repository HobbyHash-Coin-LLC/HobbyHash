var events = require('events');
var crypto = require('crypto');
var SHA3 = require('sha3');
var async = require('async');
var http = require('http');

var bignum = require('bignum');
var BigInt = require('big-integer');

var util = require('./util.js');
var daemon = require('./daemon.js');
var blockTemplate = require('./blockTemplate.js');

// Unique extranonce per subscriber
var ExtraNonceCounter = function () {
  this.next = function () {
  return(crypto.randomBytes(3).toString('hex'));
  };
};

//Unique job per new block template
var JobCounter = function () {
  var counter = 0x0000cccc;

  this.next = function () {
  counter++;
  if (counter % 0xffffffffff === 0) counter = 1;
  return this.cur();
  };

  this.cur = function () {
    var counter_buf = Buffer.alloc(32, 0);
    counter_buf.writeUInt32BE(counter >>> 0, 28);
    return counter_buf.toString('hex');
  };
};
function isHexString(s) {
  var check = String(s).toLowerCase();
  if(check.length % 2) {
    return false;
  }
  for (i = 0; i < check.length; i=i+2) {
  var c = check[i] + check[i+1];
  if (!isHex(c))
    return false;
  }
  return true;
}
function isHex(c) {
  var a = parseInt(c,16);
  var b = a.toString(16).toLowerCase();
  if(b.length % 2) { b = '0' + b; }
  if (b !== c) { return false; }
  return true;
}

function jobHeaderHash(job) {
  return util.kawPowHeaderHashHex(job.rpcData, job.merkleRoot);
}

/**
 * Emits:
 * - newBlock(blockTemplate) - When a new block (previously unknown to the JobManager) is added, use this event to broadcast new jobs
 * - share(shareData, blockHex) - When a worker submits a share. It will have blockHex if a block was found
 **/
var JobManager = module.exports = function JobManager(options) {

  var emitLog = function (text) { _this.emit('log', 'debug', text); };
  var emitWarningLog = function (text) { _this.emit('log', 'warning', text); };
  var emitErrorLog = function (text) { _this.emit('log', 'error', text); };
  var emitSpecialLog = function (text) { _this.emit('log', 'special', text); };

  //private members
  var _this = this;
  var jobCounter = new JobCounter();

  function SetupJobDaemonInterface(finishedCallback) {

    if (!Array.isArray(options.daemons) || options.daemons.length < 1) {
      emitErrorLog('No daemons have been configured - pool cannot start');
      return;
    }

    _this.daemon = new daemon.interface(options.daemons, function (severity, message) {
      _this.emit('log', severity, message);
    });

    _this.daemon.once('online', function () {
      // console.log("The util daemon is alive.");
      finishedCallback();
    }).on('connectionFailed', function (error) {
      emitErrorLog('Failed to connect daemon(s): ' + JSON.stringify(error));
    }).on('error', function (message) {
      emitErrorLog(message);
    });
    _this.daemon.init();
  }

  SetupJobDaemonInterface(function () {});

  var shareMultiplier = algos[options.coin.algorithm].multiplier;

  // Actual hash difficulty from PoW digest (powLimit / digest).
  // Do NOT use bignum.toNumber() here — digests are 256-bit and collapse to
  // junk (~1e-9), which sharelog then replaces with the assigned share diff.
  function formatShareDifficultyFromDigest(digestHex) {
    var normalized = String(digestHex || '').replace(/^0x/i, '').toLowerCase();
    if (!normalized || !/^[0-9a-f]+$/.test(normalized)) {
      return undefined;
    }
    var shareDiff = util.kawpowNetworkDifficultyFromTargetHex(normalized) * shareMultiplier;
    if (!isFinite(shareDiff) || shareDiff <= 0) {
      return undefined;
    }
    return shareDiff.toFixed(8);
  }

  //public members

  this.extraNonceCounter = new ExtraNonceCounter();

  this.currentJob;
  this.validJobs = {};

  var hashDigest = algos[options.coin.algorithm].hash(options.coin);

  var coinbaseHasher = (function () {
    switch (options.coin.algorithm) {
      default:
        return util.sha256d;
    }
  })();


  var blockHasher = (function () {
    switch (options.coin.algorithm) {
      case 'sha1':
        return function (d) {
          return util.reverseBuffer(util.sha256d(d));
        };
      default:
        return function (d) {
          return util.reverseBuffer(util.sha256(d));
        };
    }
  })();

  this.updateCurrentJob = function (rpcData) {
    if (String(rpcData.powalgo || '').toLowerCase() !== 'kawpow') {
      return;
    }
    var tmpBlockTemplate = new blockTemplate(
      jobCounter.next(),
      rpcData,
      options.coin.reward,
      options.recipients,
      options.address
    );

    _this.currentJob = tmpBlockTemplate;
    _this.emit('updatedBlock', tmpBlockTemplate, true);
    _this.validJobs[tmpBlockTemplate.jobId] = tmpBlockTemplate;

  };

  //returns true if processed a new block
  this.processTemplate = function (rpcData) {

    if (String(rpcData.powalgo || '').toLowerCase() !== 'kawpow') {
      return false;
    }

    /* Block is new if A) its the first block we have seen so far or B) the blockhash is different and the
     block height is greater than the one we have */
    var isNewBlock = typeof(_this.currentJob) === 'undefined';
    if (!isNewBlock && _this.currentJob.rpcData.previousblockhash !== rpcData.previousblockhash) {
      isNewBlock = true;

      //If new block is outdated/out-of-sync than return
      if (rpcData.height < _this.currentJob.rpcData.height) return false;
    }

    if (!isNewBlock) return false;


    var tmpBlockTemplate = new blockTemplate(
      jobCounter.next(),
      rpcData,
      options.coin.reward,
      options.recipients,
      options.address
    );

    this.currentJob = tmpBlockTemplate;

    this.validJobs = {};
    _this.emit('newBlock', tmpBlockTemplate);

    this.validJobs[tmpBlockTemplate.jobId] = tmpBlockTemplate;

    return true;

  };

  /** Drop active jobs when the chain enters a SHA window (GPU pool must not assign work). */
  this.clearWork = function () {
    _this.currentJob = undefined;
    _this.validJobs = {};
  };

  this.processShare = function (miner_given_jobId, previousDifficulty, difficulty, miner_given_nonce, ipAddress, port, workerName, miner_given_header, miner_given_mixhash, extraNonce1, callback_parent, shareOptions) {

    var submitTime = Date.now() / 1000 | 0;

    shareOptions = shareOptions || {};

    var shareError = function (error) {
      _this.emit('share', {
          job: miner_given_jobId,
          ip: ipAddress,
          worker: workerName,
          difficulty: difficulty,
          error: error[1]
      });
      callback_parent( {error: error, result: null});
      return;
    };

    var job = this.validJobs[miner_given_jobId];
    // Hot path: do not JSON.stringify the full job (includes all txs) per share —
    // that was inflating stratum.log to multi‑GB and burning local I/O during KawPow.

    if (typeof job === 'undefined' || job.jobId != miner_given_jobId)
      return shareError([20, 'job not found']);

    if (String(job.rpcData.powalgo || '').toLowerCase() !== 'kawpow') {
      return shareError([20, 'gpu mining paused for sha block height']);
    }

    // Per-worker coinbase identity changes merkle/header_hash — rebuild for this miner.
    var minerBind = job.bindMiner(workerName, shareOptions.userAgent || '');
    var header_hash = minerBind.headerHash;

    if (job.curTime < (submitTime - 600))
      return shareError([20, 'job is too old']);

    if (!isHexString(miner_given_header))
      return shareError([20, 'invalid header hash, must be hex']);
        
    if (miner_given_header && miner_given_header.length > 0 &&
        header_hash.toLowerCase() !== String(miner_given_header).toLowerCase())
      return shareError([20, 'invalid header hash']);
    
    if (!isHexString(miner_given_nonce))
      return shareError([20, 'invalid nonce, must be hex']);
    
    if (miner_given_mixhash && miner_given_mixhash.length > 0 && !isHexString(miner_given_mixhash))
      return shareError([20, 'invalid mixhash, must be hex']);
    
    if (miner_given_nonce.length !== 16)
      return shareError([20, 'incorrect size of nonce, must be 8 bytes']);
    
    if (miner_given_mixhash && miner_given_mixhash.length > 0 && miner_given_mixhash.length !== 64)
      return shareError([20, 'incorrect size of mixhash, must be 32 bytes']);

    if (!miner_given_mixhash || miner_given_mixhash.length !== 64)
      miner_given_mixhash = '0'.repeat(64);

    if (!shareOptions.skipExtranonceCheck && extraNonce1 && miner_given_nonce.indexOf(String(extraNonce1).toLowerCase()) !== 0)
      return shareError([24, 'nonce out of worker range']);

    if (!job.registerSubmit(header_hash.toLowerCase(), miner_given_nonce.toLowerCase()))
      return shareError([22, 'duplicate share']);

    var target_share_hex = util.kawpowShareTargetHex(difficulty);
    
    var blockHashInvalid;
    var blockHash;
    var blockHex;

    if (options.kawpow_validator == "kawpowd") {

      var shareDigest = '';
      async.series([
        function(callback) {
          var kawpowd_url = 'http://'+options.kawpow_wrapper_host+":"+options.kawpow_wrapper_port+'/'+'?header_hash='+header_hash+'&mix_hash='+miner_given_mixhash+'&nonce='+miner_given_nonce+'&height='+job.rpcData.height+'&share_boundary='+target_share_hex+'&block_boundary='+job.target_hex;
  
          http.get(kawpowd_url, function (res) {
          res.setEncoding("utf8");
          let body = "";
          res.on("data", data => {
            body += data;
          });
          res.on("end", () => {
            body = JSON.parse(body);
            if (body.result !== true) {
              callback('kawpow mix hash mismatch', false);
              return shareError([20, 'kawpow validation failed']);
            }
            shareDigest = String(body.digest || '').replace(/^0x/i, '').toLowerCase();
            if (body.share !== true && body.block !== true) {
              callback('kawpow share did not meet job or block difficulty level', false);
              return shareError([20, 'kawpow validation failed']);
            }

            if (body.block === true) {
              var submitMix = body.mix_hash || miner_given_mixhash;
              blockHex = job.serializeBlock(header_hash, miner_given_nonce, submitMix, minerBind.genTx, minerBind.merkleRoot).toString('hex');
              blockHash = body.digest;
            }
            callback(null, true);
            return;
          });
        });
      },
      function(callback) {
  
          var blockDiffAdjusted = job.difficulty * shareMultiplier
          var shareDiffFixed = formatShareDifficultyFromDigest(shareDigest || blockHash);
          _this.emit('share', {
            job: miner_given_jobId,
            ip: ipAddress,
            port: port,
            worker: workerName,
            height: job.rpcData.height,
            blockReward: job.rpcData.coinbasevalue,
            difficulty: difficulty,
            shareDiff: shareDiffFixed,
            blockDiff: blockDiffAdjusted,
            blockDiffActual: job.difficulty,
            blockHash: blockHash,
            blockHashInvalid: blockHashInvalid,
            shareDigest: shareDigest,
            nonce: miner_given_nonce,
            mixhash: miner_given_mixhash,
            headerHash: header_hash
          }, blockHex);
  
          callback_parent({result: true, error: null, blockHash: blockHash});
          callback(null, true);
          return;
      }
      ], function(err, results) {
        if (err != null) {
          emitErrorLog("kawpow verify failed, ERRORS: "+err);
          return;
        }
      });


    } else {

      _this.daemon.cmd('getkawpowhash', [ header_hash, miner_given_mixhash, miner_given_nonce, job.rpcData.height, job.target_hex ], function (results) {

        var digest = results[0].response.digest;
        var result = results[0].response.result;
        var mix_hash = results[0].response.mix_hash;
        var meets_target = results[0].response.meets_target;

        if (result == 'true') {
          // console.log("SHARE IS VALID");
          let headerBigNum = BigInt(result, 32);
          if (job.target.ge(headerBigNum)) {
            // console.log("BLOCK CANDIDATE");
            var blockHex = job.serializeBlock(header_hash, miner_given_nonce, mix_hash, minerBind.genTx, minerBind.merkleRoot).toString('hex');
            var blockHash = digest;
          }
          var blockDiffAdjusted = job.difficulty * shareMultiplier
          var shareDigestHex = String(digest || '').replace(/^0x/i, '').toLowerCase();
          var shareDiffFixed = formatShareDifficultyFromDigest(shareDigestHex || blockHash);

          _this.emit('share', {
              job: miner_given_jobId,
              ip: ipAddress,
              port: port,
              worker: workerName,
              height: job.rpcData.height,
              blockReward: job.rpcData.coinbasevalue,
              difficulty: difficulty,
              shareDiff: shareDiffFixed,
              blockDiff: blockDiffAdjusted,
              blockDiffActual: job.difficulty,
              blockHash: blockHash,
              blockHashInvalid: blockHashInvalid,
              shareDigest: shareDigestHex,
              nonce: miner_given_nonce,
              mixhash: mix_hash,
              headerHash: header_hash
          }, blockHex);

          // return {result: true, error: null, blockHash: blockHash};
          // callback_parent( {error: error, result: null});
          callback_parent({result: true, error: null, blockHash: blockHash});

        } else {
          // console.log("SHARE FAILED");
          return shareError([20, 'bad share: invalid hash']);
        }


      });
    }
  };

  this.findJobByHeaderHash = function (headerHash) {
    var normalized = String(headerHash || '').toLowerCase().replace(/^0x/, '');
    var jobId;

    for (jobId in _this.validJobs) {
      var candidate = _this.validJobs[jobId];
      if (candidate.findBindByHeaderHash && candidate.findBindByHeaderHash(normalized))
        return candidate;
      var computed = jobHeaderHash(candidate);
      if (computed === normalized) {
        return candidate;
      }
    }

    return null;
  };

  this.findJobByShortId = function (shortId) {
    var normalized = String(shortId || '').toLowerCase().replace(/^0x/, '');
    var jobId;
    var match = null;

    if (!normalized) {
      return null;
    }

    for (jobId in _this.validJobs) {
      var candidate = _this.validJobs[jobId];
      var headerPrefix = jobHeaderHash(candidate).slice(0, normalized.length);
      if (headerPrefix === normalized) {
        match = candidate;
      }
      if (candidate.findBindByHeaderPrefix && candidate.findBindByHeaderPrefix(normalized)) {
        match = candidate;
      }
      if (String(jobId).toLowerCase().slice(-normalized.length) === normalized) {
        match = _this.validJobs[jobId];
      }
    }

    return match;
  };
};
JobManager.prototype.__proto__ = events.EventEmitter.prototype;
