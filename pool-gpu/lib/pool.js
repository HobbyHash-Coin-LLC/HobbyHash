var events = require('events');
var async = require('async');
const { spawn } = require('child_process');

var varDiff = require('./varDiff.js');
var daemon = require('./daemon.js');
var stratum = require('./stratum.js');
var jobManager = require('./jobManager.js');
var util = require('./util.js');

/*process.on('uncaughtException', function(err) {
 console.log(err.stack);
 throw err;
 });*/

var pool = module.exports = function pool(options, authorizeFn) {

    this.options = options;

    var _this = this;
    var blockPollingIntervalId;
    var idlePingIntervalId;
    var shaWindowIdle = false;
    
    this.progpow_wrapper = null;


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


    if (!(options.coin.algorithm in algos)) {
        emitErrorLog('The ' + options.coin.algorithm + ' hashing algorithm is not supported.');
        throw new Error();
    }


    this.start = function () {
        SetupVarDiff();
        SetupApi();
        SetupDaemonInterface(function () {
            DetectCoinData(function () {
                SetupRecipients();
                SetupJobManager();
                OnBlockchainSynced(function () {
                    GetFirstJob(function () {
                        SetupBlockPolling();
                        SetupIdleKeepalive();
                        StartStratumServer(function () {
                            OutputPoolInfo();
                            _this.emit('started');
                        });
                    });
                });
            });
        });
    };


    function GetFirstJob(finishedCallback) {

        GetBlockTemplate(function (error, result) {
            if (error) {
                emitErrorLog('Error with getblocktemplate on creating first job, server cannot start');
                return;
            }

            var portWarnings = [];

            var networkDiffAdjusted = options.initStats.difficulty;

            Object.keys(options.ports).forEach(function (port) {
                var portDiff = options.ports[port].diff;
                if (networkDiffAdjusted < portDiff)
                    portWarnings.push('port ' + port + ' w/ diff ' + portDiff);
            });

            //Only let the first fork show synced status or the log wil look flooded with it
            if (portWarnings.length > 0 && (!process.env.forkId || process.env.forkId === '0')) {
                var warnMessage = 'Network diff of ' + networkDiffAdjusted + ' is lower than '
                    + portWarnings.join(' and ');
                emitWarningLog(warnMessage);
            }

            finishedCallback();

        });
    }


    function OutputPoolInfo() {

        var startMessage = 'Stratum Pool Server Started for ' + options.coin.name +
            ' [' + options.coin.symbol.toUpperCase() + '] {' + options.coin.algorithm + '}';
        if (process.env.forkId && process.env.forkId !== '0') {
            emitLog(startMessage);
            return;
        }
        var infoLines = [startMessage,
            'Network Connected:\t' + (options.testnet ? 'Testnet' : 'Mainnet'),
            'Detected Reward Type:\t' + options.coin.reward,
            'Current Block Height:\t' + (_this.jobManager.currentJob ? _this.jobManager.currentJob.rpcData.height : '(idle — SHA window)'),
            'Current Block Diff:\t' + (_this.jobManager.currentJob ? _this.jobManager.currentJob.difficulty * algos[options.coin.algorithm].multiplier : 'n/a'),
            'Current Connect Peers:\t' + options.initStats.connections,
            'Network Hash Rate:\t' + util.getReadableHashRateString(options.initStats.networkHashRate),
            'Stratum Port(s):\t' + _this.options.initStats.stratumPorts.join(', ')
        ];

        if (typeof options.blockRefreshInterval === "number" && options.blockRefreshInterval > 0)
            infoLines.push('Block polling every:\t' + options.blockRefreshInterval + ' ms');

        emitSpecialLog(infoLines.join('\n\t\t\t\t\t\t'));
    }


    function OnBlockchainSynced(syncedCallback) {

        var checkSynced = function (displayNotSynced) {
            _this.daemon.cmd('getblocktemplate', [{"capabilities": [ "coinbasetxn", "workid", "coinbase/append" ], "rules": [ "segwit" ]}], function (results) {
                var synced = results.every(function (r) {
                    return !r.error || r.error.code !== -10;
                });
                if (synced) {
                    syncedCallback();
                }
                else {
                    if (displayNotSynced) displayNotSynced();
                    setTimeout(checkSynced, 5000);

                    //Only let the first fork show synced status or the log wil look flooded with it
                    if (!process.env.forkId || process.env.forkId === '0')
                        generateProgress();
                }

            });
        };
        checkSynced(function () {
            //Only let the first fork show synced status or the log wil look flooded with it
            if (!process.env.forkId || process.env.forkId === '0')
                emitErrorLog('Daemon is still syncing with network (download blockchain) - server will be started once synced');
        });


        var generateProgress = function () {

            _this.daemon.cmd('getblockchaininfo', [], function (results) {
                var blockCount = results.sort(function (a, b) {
                    return b.response.blocks - a.response.blocks;
                })[0].response.blocks;

                //get list of peers and their highest block height to compare to ours
                _this.daemon.cmd('getpeerinfo', [], function (results) {

                    var peers = results[0].response;
                    var totalBlocks = peers.sort(function (a, b) {
                        return b.startingheight - a.startingheight;
                    })[0].startingheight;

                    var percent = (blockCount / totalBlocks * 100).toFixed(2);
                    emitWarningLog('Downloaded ' + percent + '% of blockchain from ' + peers.length + ' peers');
                });

            });
        };

    }


    function SetupApi() {
        if (typeof(options.api) !== 'object' || typeof(options.api.start) !== 'function') {
        } else {
            options.api.start(_this);
        }
    }

    function SetupVarDiff() {
        _this.varDiff = {};
        Object.keys(options.ports).forEach(function (port) {
            if (options.ports[port].varDiff)
                _this.setVarDiff(port, options.ports[port].varDiff);
        });
    }

    /*
     Coin daemons either use submitblock or getblocktemplate for submitting new blocks
     */
    function SubmitBlock(blockHex, callback) {

        console.log("submitblock "+blockHex);

        var rpcCommand, rpcArgs;
        if (options.hasSubmitMethod) {
            rpcCommand = 'submitblock';
            rpcArgs = [blockHex];
        }
        else {
            rpcCommand = 'getblocktemplate';
            rpcArgs = [{'mode': 'submit', 'data': blockHex}];
        }


        _this.daemon.cmd(rpcCommand,
            rpcArgs,
            function (results) {
                for (var i = 0; i < results.length; i++) {
                    var result = results[i];
                    if (result.error) {
                        emitErrorLog('rpc error with daemon instance ' +
                            result.instance.index + ' when submitting block with ' + rpcCommand + ' ' +
                            JSON.stringify(result.error)
                        );
                        return callback(result.error);
                    }
                    if (result.response === 'rejected' ||
                        (typeof result.response === 'string' && result.response.length > 0)) {
                        emitErrorLog('Daemon instance ' + result.instance.index + ' rejected block: ' + result.response);
                        return callback({reject: result.response});
                    }
                }
                emitLog('Submitted Block using ' + rpcCommand + ' successfully to daemon instance(s)');
                callback(null);
            }
        );
    }

    function SetupRecipients() {
        var recipients = [];
        options.feePercent = 0;
        options.rewardRecipients = options.rewardRecipients || {};

        for (var r in options.rewardRecipients) {
            var percent = options.rewardRecipients[r];
            var rObj = {
                percent: percent,
                address: r
            };
                recipients.push(rObj);
                options.feePercent += percent;
        }
        options.recipients = recipients;
    }

    var jobManagerLastSubmitBlockHex = false;
    var shareLogger = null;

    function getShareLogger() {
        if (!shareLogger && options.shareLogDir) {
            shareLogger = require('./sharelog.js').createShareLogger(options);
        }
        return shareLogger;
    }

    function logShareEvent(shareData, isValidShare, isValidBlock) {
        var logger = getShareLogger();
        if (!logger) {
            return;
        }
        logger.writeShare(shareData, isValidShare);
        if (isValidBlock && isValidShare) {
            logger.recordBlock(shareData);
        }
    }


    function SetupJobManager() {

        _this.jobManager = new jobManager(options);

        _this.jobManager.on('newBlock', function (blockTemplate) {
            //Check if stratumServer has been initialized yet
            if (_this.stratumServer && shouldAssignWork()) {
                _this.stratumServer.broadcastMiningJobs(blockTemplate.getJobParams().slice());
            }
        }).on('updatedBlock', function (blockTemplate) {
            //Check if stratumServer has been initialized yet
            if (_this.stratumServer && shouldAssignWork()) {
                var job = blockTemplate.getJobParams().slice();
                // Let the miners keep existing work.
                job[4] = false;

                _this.stratumServer.broadcastMiningJobs(job);
            }
        }).on('share', function (shareData, blockHex) {
            var isValidShare = !shareData.error;
            var isValidBlock = !!blockHex;
            var emitShare = function () {
                logShareEvent(shareData, isValidShare, isValidBlock);
                _this.emit('share', isValidShare, isValidBlock, shareData);
            };

            /*
             If we calculated that the block solution was found,
             before we emit the share, lets submit the block,
             then check if it was accepted using RPC getblock
             */
            if (!isValidBlock)
                emitShare();
            else {
                if (jobManagerLastSubmitBlockHex === blockHex) {
                    emitWarningLog('Warning, ignored duplicate submit block'); // + blockHex); //<< blockHex could be huge
                } else {
                    jobManagerLastSubmitBlockHex = blockHex;
                    SubmitBlock(blockHex, function (submitErr) {
                        if (submitErr) {
                            shareData.error = submitErr;
                            logShareEvent(shareData, isValidShare, false);
                            _this.emit('share', isValidShare, false, shareData);
                            if (options.getNewBlockAfterFound === true) {
                                GetBlockTemplate(function () {});
                            }
                            return;
                        }
                        emitSpecialLog('Solved and confirmed block ' + shareData.height + ' by ' + shareData.worker);
                        logShareEvent(shareData, true, true);
                        CheckBlockAccepted(shareData.height, function (isAccepted, tx) {
                            var accepted = isAccepted === true;
                            if (accepted) {
                                shareData.txHash = tx;
                            } else {
                                shareData.error = tx;
                            }
                            _this.emit('share', true, accepted, shareData);
                            if (options.getNewBlockAfterFound === true) {
                                GetBlockTemplate(function (error, result, foundNewBlock) {
                                    if (foundNewBlock) {
                                        emitLog('Block notification via RPC after block submission');
                                    }
                                });
                            }
                        });
                    });
                }
            }
        }).on('log', function (severity, message) {
            _this.emit('log', severity, message);
        });
    }


    function SetupDaemonInterface(finishedCallback) {

        if (!Array.isArray(options.daemons) || options.daemons.length < 1) {
            emitErrorLog('No daemons have been configured - pool cannot start');
            return;
        }

        _this.daemon = new daemon.interface(options.daemons, function (severity, message) {
            _this.emit('log', severity, message);
        });

        _this.daemon.once('online', function () {
            finishedCallback();

        }).on('connectionFailed', function (error) {
            emitErrorLog('Failed to connect daemon(s): ' + JSON.stringify(error));

        }).on('error', function (message) {
            emitErrorLog(message);

        });

        _this.daemon.init();
    }


    function DetectCoinData(finishedCallback) {

        var batchRpcCalls = [
            ['validateaddress', [options.address]],
            ['getdifficulty', []],
            ['getnetworkinfo', []],
            ['getblockchaininfo', []],
            ['getmininginfo', []],
            ['submitblock', []]
        ];

        _this.daemon.batchCmd(batchRpcCalls, function (error, results) {
            if (error || !results) {
                emitErrorLog('Could not start pool, error with init batch RPC call: ' + JSON.stringify(error));
                return;
            }

            var rpcResults = {};

            for (var i = 0; i < results.length; i++) {
                var rpcCall = batchRpcCalls[i][0];
                var r = results[i];
                rpcResults[rpcCall] = r.result || r.error;

                if (rpcCall !== 'submitblock' && (r.error || !r.result)) {
                    emitErrorLog('Could not start pool, error with init RPC ' + rpcCall + ' - ' + JSON.stringify(r.error));
                    return;
                }
            }

            if (!rpcResults.validateaddress.isvalid) {
                emitErrorLog('Daemon reports address is not valid');
                return;
            }

                if (isNaN(rpcResults.getdifficulty) && 'proof-of-stake' in rpcResults.getdifficulty)
                    options.coin.reward = 'POS';
                else
                    options.coin.reward = 'POW';


            /* POS coins must use the pubkey in coinbase transaction, and pubkey is
             only given if address is owned by wallet.*/
            if (options.coin.reward === 'POS' && typeof(rpcResults.validateaddress.pubkey) === 'undefined') {
                emitErrorLog('The address provided is not from the daemon wallet - this is required for POS coins.');
                return;
            }

            options.poolAddressScript = (function () {
                var v = rpcResults.validateaddress;
                if (v.scriptPubKey) {
                    return Buffer.from(v.scriptPubKey, 'hex');
                }
                return util.addressToScript(v.address);
            })();

            options.testnet = rpcResults.getblockchaininfo.chain === 'test' || rpcResults.getblockchaininfo.chain === 'regtest';
            options.protocolVersion = rpcResults.getnetworkinfo.protocolversion;

            options.initStats = {
                connections: rpcResults.getnetworkinfo.connections,
                difficulty: rpcResults.getmininginfo.difficulty * algos[options.coin.algorithm].multiplier,
                networkHashRate: rpcResults.getmininginfo.networkhashps
            };


            if (rpcResults.submitblock.message === 'Method not found') {
                options.hasSubmitMethod = false;
            }
            else if (rpcResults.submitblock.code === -1) {
                options.hasSubmitMethod = true;
            }
            else {
                emitErrorLog('Could not detect block submission RPC method, ' + JSON.stringify(results));
                return;
            }

            finishedCallback();

        });
    }


    function StartStratumServer(finishedCallback) {
        options.getJobParams = function () {
            if (!_this.jobManager.currentJob) {
                return null;
            }
            return _this.jobManager.currentJob.getJobParams();
        };
        options.getJobParamsForClient = function (client, baseParams) {
            if (!_this.jobManager.currentJob || !client)
                return baseParams || null;
            var params = _this.jobManager.currentJob.getJobParamsForMiner(
                client.workerName || '',
                client.minerUserAgent || ''
            );
            if (baseParams && baseParams.length > 4 && baseParams[4] === false)
                params[4] = false;
            return params;
        };

        _this.stratumServer = new stratum.Server(options, authorizeFn);

        _this.stratumServer.on('started', function () {
            options.initStats.stratumPorts = Object.keys(options.ports);
            if (shouldAssignWork()) {
                _this.stratumServer.broadcastMiningJobs(_this.jobManager.currentJob.getJobParams());
            }
            finishedCallback();

        }).on('broadcastTimeout', function () {
            if (shaWindowIdle) {
                return;
            }
            emitLog('No new blocks for ' + options.jobRebroadcastTimeout + ' seconds - updating transactions & rebroadcasting work');

            GetBlockTemplate(function (error, rpcData, processedBlock) {
                if (error || processedBlock || !rpcData || !isKawPowTemplate(rpcData)) return;
                _this.jobManager.updateCurrentJob(rpcData);
            });

        }).on('client.connected', function (client) {
            if (typeof(_this.varDiff[client.socket.localPort]) !== 'undefined') {
                _this.varDiff[client.socket.localPort].manageClient(client);
            }

            client.on('difficultyChanged', function (diff) {
                _this.emit('difficultyUpdate', client.workerName, diff);

            }).on('subscription', function (params, resultCallback) {

                var extraNonce = _this.jobManager.extraNonceCounter.next();
                resultCallback(null,
                    extraNonce,
                    extraNonce
                );

            }).on('authorization', function (params) {
                var portDiff;
                if (typeof(options.ports[client.socket.localPort]) !== 'undefined' && options.ports[client.socket.localPort].diff) {
                    portDiff = options.ports[client.socket.localPort].diff;
                } else {
                    portDiff = 8;
                }
                client.sendExtranonce();
                client.sendDifficulty(portDiff);
                if (shouldAssignWork()) {
                    client.sendMiningJob(options.getJobParamsForClient(client, null));
                } else if (shaWindowIdle) {
                    client.sendPing();
                }
            }).on('submit', function (params, resultCallback) {
                if (shaWindowIdle || !_this.jobManager.currentJob) {
                    resultCallback({ code: 21, message: 'gpu mining paused for sha block height' }, false);
                    return;
                }
                var jobId = params.jobId;
                var headerHash = params.header;
                var mixhash = params.mixhash;
                var minerBind;

                if (params.jobIdIsShort || !_this.jobManager.validJobs[jobId]) {
                    var shortJob = _this.jobManager.findJobByShortId(jobId);
                    if (shortJob) {
                        jobId = shortJob.jobId;
                        minerBind = shortJob.bindMiner(client.workerName || '',
                                                       client.minerUserAgent || '');
                        headerHash = minerBind.headerHash;
                    }
                }

                if (!headerHash || headerHash === '0') {
                    var headerJob = _this.jobManager.validJobs[jobId];
                    if (headerJob) {
                        minerBind = headerJob.bindMiner(client.workerName || '',
                                                        client.minerUserAgent || '');
                        headerHash = minerBind.headerHash;
                    }
                }

                if (!mixhash || mixhash === '0' || String(mixhash).length < 64) {
                    mixhash = '0'.repeat(64);
                }

                // Use authorized workerName (same string used when minting the job), not the
                // raw submit name — getSafeWorkerString may rewrite multi-dot worker names.
                _this.jobManager.processShare(
                    jobId,
                    client.previousDifficulty,
                    client.difficulty,
                    params.nonce,
                    client.remoteAddress,
                    client.socket.localPort,
                    client.workerName || params.name,
                    headerHash,
                    mixhash,
                    client.extraNonce1
                , function (result) {
                    resultCallback(result.error, result.result ? true : null);
                }, { userAgent: client.minerUserAgent || '' });

            }).on('submitEth', function (params, resultCallback) {
                if (shaWindowIdle || !_this.jobManager.currentJob) {
                    resultCallback({ code: 21, message: 'gpu mining paused for sha block height' }, false);
                    return;
                }
                var job = _this.jobManager.findJobByHeaderHash(params.header);
                if (!job) {
                    resultCallback({ code: 23, message: 'stale share' }, false);
                    return;
                }

                _this.jobManager.processShare(
                    job.jobId,
                    client.previousDifficulty,
                    client.difficulty,
                    params.nonce,
                    client.remoteAddress,
                    client.socket.localPort,
                    client.workerName || params.name,
                    params.header,
                    params.mixhash,
                    client.extraNonce1,
                    function (result) {
                        if (result.error) {
                            resultCallback({ code: 20, message: String(result.error[1] || result.error) }, false);
                            return;
                        }
                        resultCallback(null, result.result ? true : false);
                    },
                    { skipExtranonceCheck: true, userAgent: client.minerUserAgent || '' }
                );

            }).on('malformedMessage', function (message) {
                emitWarningLog('Malformed message from ' + client.getLabel() + ': ' + message);

            }).on('socketError', function (err) {
                emitWarningLog('Socket error from ' + client.getLabel() + ': ' + JSON.stringify(err));

            }).on('socketTimeout', function (reason) {
                emitWarningLog('Connected timed out for ' + client.getLabel() + ': ' + reason)

            }).on('socketDisconnect', function () {
                //emitLog('Socket disconnected from ' + client.getLabel());

            }).on('kickedBannedIP', function (remainingBanTime) {
                emitLog('Rejected incoming connection from ' + client.remoteAddress + ' banned for ' + remainingBanTime + ' more seconds');

            }).on('forgaveBannedIP', function () {
                emitLog('Forgave banned IP ' + client.remoteAddress);

            }).on('unknownStratumMethod', function (fullMessage) {
                emitLog('Unknown stratum method from ' + client.getLabel() + ': ' + fullMessage.method);

            }).on('socketFlooded', function () {
                emitWarningLog('Detected socket flooding from ' + client.getLabel());

            }).on('tcpProxyError', function (data) {
                emitErrorLog('Client IP detection failed, tcpProxyProtocol is enabled yet did not receive proxy protocol message, instead got data: ' + data);

            }).on('bootedBannedWorker', function () {
                emitWarningLog('Booted worker ' + client.getLabel() + ' who was connected from an IP address that was just banned');

            }).on('triggerBan', function (reason) {
                emitWarningLog('Banned triggered for ' + client.getLabel() + ': ' + reason);
                _this.emit('banIP', client.remoteAddress, client.workerName);
            });
        });
    }


    function SetupBlockPolling() {
        if (typeof options.blockRefreshInterval !== "number" || options.blockRefreshInterval <= 0) {
            emitLog('Block template polling has been disabled');
            return;
        }

        var pollingInterval = options.blockRefreshInterval;

        blockPollingIntervalId = setInterval(function () {
            GetBlockTemplate(function (error, result, foundNewBlock) {
                if (foundNewBlock)
                    emitLog('Block notification via RPC polling');
            });
        }, pollingInterval);
    }

    /**
     * During SHA windows (idleDuringSha), broadcast mining.ping so GPU miners stay
     * connected without issuing KawPow work — same pattern as ckpool SHA idle ping.
     */
    function SetupIdleKeepalive() {
        if (!options.idleDuringSha) {
            return;
        }
        var sec = options.idlePingIntervalSec;
        if (typeof sec !== 'number' || sec <= 0) {
            sec = 29;
        }
        idlePingIntervalId = setInterval(function () {
            if (!shaWindowIdle || !_this.stratumServer) {
                return;
            }
            var clients = _this.stratumServer.getStratumClients();
            var hasAuthorized = false;
            for (var clientId in clients) {
                if (clients[clientId].authorized) {
                    hasAuthorized = true;
                    break;
                }
            }
            if (!hasAuthorized) {
                return;
            }
            _this.stratumServer.broadcastPing();
        }, sec * 1000);
    }


    function rpcPowAlgo(rpcData) {
        return String(rpcData && rpcData.powalgo ? rpcData.powalgo : '').toLowerCase();
    }

    function isKawPowTemplate(rpcData) {
        return rpcPowAlgo(rpcData) === 'kawpow';
    }

    function enterShaIdleWindow(rpcData) {
        var wasIdle = shaWindowIdle;
        shaWindowIdle = true;
        if (_this.jobManager && typeof _this.jobManager.clearWork === 'function') {
            _this.jobManager.clearWork();
        }
        if (_this.stratumServer) {
            _this.stratumServer.clearJobRebroadcast();
            if (!wasIdle) {
                _this.stratumServer.broadcastPing();
            }
        }
        if (!wasIdle) {
            emitLog('Height ' + rpcData.height + ' is ' + rpcPowAlgo(rpcData) + ' — GPU pool idle during SHA window (work cleared)');
        }
    }

    function leaveShaIdleWindow(rpcData) {
        if (shaWindowIdle) {
            emitLog('Height ' + rpcData.height + ' is kawpow — GPU pool resuming work');
        }
        shaWindowIdle = false;
    }

    function shouldAssignWork() {
        return !shaWindowIdle && _this.jobManager && _this.jobManager.currentJob;
    }

    function GetBlockTemplate(callback) {
        fetchBlockTemplate(callback, 0);
    }

    function fetchBlockTemplate(callback, shaAdvanceDepth) {
        // HOBC V6: request the "kawpow" GBT rule. Below the multi-algo race activation height the
        // node ignores it and returns sha256 on SHA (dual-PoW) heights, so the existing idle/window
        // behavior is preserved. At/after activation the node honors the rule and always returns a
        // KawPow template, so this GPU pool mines KawPow continuously (no more SHA-window idling) —
        // matching the race where SHA/KawPow/RandomX all mine simultaneously.
        _this.daemon.cmd('getblocktemplate',
            [{"capabilities": ["coinbasetxn", "workid", "coinbase/append"], "rules": ["segwit", "kawpow"]}],
            function (result) {
                if (result.error) {
                    logger.debug("result.error = %s", result);
                    logger.error('getblocktemplate call failed for daemon instance ' +
                        result.instance.index + ' with error ' + JSON.stringify(result.error));
                    callback(result.error);
                    return;
                }

                var rpcData = result.response;
                if (!isKawPowTemplate(rpcData)) {
                    if (options.idleDuringSha) {
                        enterShaIdleWindow(rpcData);
                        callback(null, rpcData, false);
                        return;
                    }
                    if (shaAdvanceDepth >= 6) {
                        emitErrorLog('Preview pool stuck in non-kawpow window at height ' + rpcData.height + ' (' + rpcPowAlgo(rpcData) + ')');
                        callback(new Error('preview pool: non-kawpow window'));
                        return;
                    }
                    emitLog('Height ' + rpcData.height + ' is ' + rpcPowAlgo(rpcData) + ' — mining SHA block on regtest backend for GPU window');
                    _this.daemon.cmd('generatetoaddress', [1, options.address], function (mineResult) {
                        if (mineResult.error) {
                            emitErrorLog('generatetoaddress failed at height ' + rpcData.height + ': ' + JSON.stringify(mineResult.error));
                            callback(mineResult.error);
                            return;
                        }
                        fetchBlockTemplate(callback, shaAdvanceDepth + 1);
                    });
                    return;
                }

                leaveShaIdleWindow(rpcData);
                var processedNewBlock = _this.jobManager.processTemplate(rpcData);
                callback(null, rpcData, processedNewBlock);
            }, true
        );
    }

    function CheckBlockAccepted(expectedHeight, callback, attempt) {
        attempt = attempt || 0;
        _this.daemon.cmd('getblockchaininfo',
            [],
            function (results) {
                var validResults = results.filter(function (result) {
                    return result.response && Number.isFinite(Number(result.response.blocks));
                });
                if (validResults.length >= 1 && Number(validResults[0].response.blocks) >= Number(expectedHeight)) {
                    callback(true, null);
                    return;
                }
                if (attempt < 8) {
                    setTimeout(function () {
                        CheckBlockAccepted(expectedHeight, callback, attempt + 1);
                    }, 400);
                    return;
                }
                callback(false, {"unknown": "check coin daemon logs"});
            }
        );
    }


    /**
     * This method is being called from the blockNotify so that when a new block is discovered by the daemon
     * We can inform our miners about the newly found block
     **/
     this.processBlockNotify = function(blockHash, sourceTrigger) {
        emitLog('Block notification via ' + sourceTrigger);
        if (typeof(_this.jobManager) !== 'undefined'){
            if (typeof(_this.jobManager.currentJob) !== 'undefined' && blockHash !== _this.jobManager.currentJob.rpcData.previousblockhash){
                GetBlockTemplate(function(error, result){
                    if (error)
                        emitErrorLog('Block notify error getting block template for ' + options.coin.name);
                });
            }
        }
    };

    this.relinquishMiners = function (filterFn, resultCback) {
        var origStratumClients = this.stratumServer.getStratumClients();

        var stratumClients = [];
        Object.keys(origStratumClients).forEach(function (subId) {
            stratumClients.push({subId: subId, client: origStratumClients[subId]});
        });
        async.filter(
            stratumClients,
            filterFn,
            function (clientsToRelinquish) {
                clientsToRelinquish.forEach(function (cObj) {
                    cObj.client.removeAllListeners();
                    _this.stratumServer.removeStratumClientBySubId(cObj.subId);
                });

                process.nextTick(function () {
                    resultCback(
                        clientsToRelinquish.map(
                            function (item) {
                                return item.client;
                            }
                        )
                    );
                });
            }
        )
    };


    this.attachMiners = function (miners) {
        miners.forEach(function (clientObj) {
            _this.stratumServer.manuallyAddStratumClient(clientObj);
        });
        _this.stratumServer.broadcastMiningJobs(_this.jobManager.currentJob.getJobParams());

    };


    this.getStratumServer = function () {
        return _this.stratumServer;
    };


    this.setVarDiff = function (port, varDiffConfig) {
        if (typeof(_this.varDiff[port]) !== 'undefined') {
            _this.varDiff[port].removeAllListeners();
        }
        _this.varDiff[port] = new varDiff(port, varDiffConfig);
        _this.varDiff[port].on('newDifficulty', function (client, newDiff) {

            /* We request to set the newDiff @ the next difficulty retarget
             (which should happen when a new job comes in - AKA BLOCK) */
            client.enqueueNextDifficulty(newDiff);

            /*if (options.varDiff.mode === 'fast'){
             //Send new difficulty, then force miner to use new diff by resending the
             //current job parameters but with the "clean jobs" flag set to false
             //so the miner doesn't restart work and submit duplicate shares
             client.sendDifficulty(newDiff);
             var job = _this.jobManager.currentJob.getJobParams();
             job[8] = false;
             client.sendMiningJob(job);
             }*/

        });
    };

};
pool.prototype.__proto__ = events.EventEmitter.prototype;
