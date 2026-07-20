var BigNum = require('bignum');
var net = require('net');
var events = require('events');
var tls = require('tls');
var fs = require('fs');

var util = require('./util.js');

var TLSoptions;

function resolveClientDifficulty(client, options) {
    var difficulty = client.difficulty;
    if (isFinite(difficulty) && difficulty > 0) {
        return difficulty;
    }
    var portKey = String(client.socket.localPort);
    if (options.portsConfig && options.portsConfig[portKey] && options.portsConfig[portKey].diff) {
        return options.portsConfig[portKey].diff;
    }
    return 0.01;
}

function stripHexPrefix(value) {
    return String(value || '').replace(/^0x/i, '');
}

function buildEthWorkParams(jobParams, difficulty) {
    if (!jobParams || jobParams.length < 7) {
        return null;
    }
    var shareTarget = util.kawPowEthTargetHex(difficulty);
    return [
        '0x' + stripHexPrefix(jobParams[1]),
        '0x' + stripHexPrefix(jobParams[2]),
        '0x' + shareTarget
    ];
}

function detectEthereumStratumVersion(params) {
    if (!params || !params.length) {
        return null;
    }
    var i;
    for (i = 0; i < params.length; i++) {
        var value = String(params[i] || '');
        if (value.indexOf('EthereumStratum/') === 0) {
            return value;
        }
    }
    return null;
}

function detectTbMinerUserAgent(params) {
    if (!params || !params.length) {
        return false;
    }
    var agent = String(params[0] || '').toLowerCase();
    return agent.indexOf('tbminer') !== -1 || agent.indexOf('teamblackminer') !== -1;
}

function resolveEthereumStratumMode(params, coinAlgorithm) {
    var explicit = detectEthereumStratumVersion(params);
    if (explicit) {
        return explicit;
    }
    // For KawPow pools, prefer native Ravencoin-style stratum unless the miner
    // explicitly requests an EthereumStratum variant.
    return null;
}

function trimExtranonceHex(value) {
    return String(value || '').replace(/^0x/i, '').slice(0, 6);
}

function shortJobIdFromParams(jobParams) {
    // TBM KawPow debug traces commonly show a short job ID that matches the
    // beginning of the header hash, not an unrelated pool-side counter.
    var headerHash = String((jobParams && jobParams[1]) || '').replace(/^0x/i, '');
    if (headerHash.length >= 6) {
        return headerHash.slice(0, 6);
    }
    return String((jobParams && jobParams[0]) || '').replace(/^0x/i, '').slice(-8);
}

var SubscriptionCounter = function(){
    var count = 0;
    var padding = 'deadbeefcafebabe';
    return {
        next: function(){
            count++;
            if (Number.MAX_VALUE === count) count = 0;
            return padding + util.packInt64LE(count).toString('hex');
        }
    };
};


/**
 * Defining each client that connects to the stratum server.
 * Emits:
 *  - subscription(obj, cback(error, extraNonce1, extraNonce2Size))
 *  - submit(data(name, jobID, extraNonce2, ntime, nonce))
**/
var StratumClient = function(options){
    var pendingDifficulty = null;
    //private members
    this.socket = options.socket;
    this.remoteAddress = options.socket.remoteAddress;
    var banning = options.banning;
    var _this = this;
    this.lastActivity = Date.now();
    this.shares = {valid: 0, invalid: 0};
    this.ethereumStratum = false;
    this.ethereumStratumVersion = null;
    this.minerUserAgent = '';
    this.isTbMiner = false;

    var considerBan = (!banning || !banning.enabled) ? function(){ return false } : function(shareValid){
        if (shareValid === true) _this.shares.valid++;
        else _this.shares.invalid++;
        var totalShares = _this.shares.valid + _this.shares.invalid;
        if (totalShares >= banning.checkThreshold){
            var percentBad = (_this.shares.invalid / totalShares) * 100;
            if (percentBad < banning.invalidPercent) //reset shares
                this.shares = {valid: 0, invalid: 0};
            else {
                _this.emit('triggerBan', _this.shares.invalid + ' out of the last ' + totalShares + ' shares were invalid');
                _this.socket.destroy();
                return true;
            }
        }
        return false;
    };

    this.init = function init(){
        setupSocket();
    };

    function handleMessage(message){
        // console.log("Received message: "+message);
        switch(message.method){
            case 'mining.subscribe':
                handleSubscribe(message);
                break;
            case 'mining.authorize':
                handleAuthorize(message);
                break;
            case 'mining.submit':
                _this.lastActivity = Date.now();
                handleSubmit(message);
                break;
            case 'eth_submitLogin':
                handleEthSubmitLogin(message);
                break;
            case 'eth_getWork':
                handleEthGetWork(message);
                break;
            case 'eth_submitWork':
                _this.lastActivity = Date.now();
                handleEthSubmitWork(message);
                break;
            case 'eth_submitHashrate':
                sendJson({
                    id: message.id,
                    jsonrpc: '2.0',
                    result: true,
                    error: null
                });
                break;
            case 'mining.get_transactions':
                sendJson({
                    id     : null,
                    result : [],
                    error  : true
                });
                break;
            case 'mining.extranonce.subscribe':
                if (_this.ethereumStratum || _this.isTbMiner) {
                    sendJson({
                        id: message.id,
                        result: true,
                        error: null
                    });
                } else {
                    sendJson({
                        id: message.id,
                        result: false,
                        error: [20, 'Not supported.', null]
                    });
                }
                break;
            case 'mining.pong':
                _this.lastActivity = Date.now();
                break;
            default:
                _this.emit('unknownStratumMethod', message);
                break;
        }
    }

    function handleSubscribe(message){
        if (_this.protocol === 'eth') {
            sendJson({
                id: message.id,
                result: true,
                error: null
            });
            return;
        }

        _this.protocol = 'stratum';
        _this.minerUserAgent = message.params && message.params.length ? String(message.params[0] || '') : '';
        _this.isTbMiner = detectTbMinerUserAgent(message.params);
        _this.ethereumStratumVersion = resolveEthereumStratumMode(
            message.params,
            options.coin && options.coin.algorithm
        );
        _this.ethereumStratum = !!_this.ethereumStratumVersion;
        if (!_this.authorized) {
            _this.requestedSubscriptionBeforeAuth = true;
        }
        _this.emit('subscription',
            {},
            function(error, extraNonce1, extraNonce1){
                if (error){
                    sendJson({
                        id: message.id,
                        result: null,
                        error: error
                    });
                    return;
                }
                if (_this.ethereumStratum) {
                    // NiceHash examples use 2-byte (4 hex) extranonce; 3 bytes is max per spec.
                    _this.extraNonce1 = trimExtranonceHex(extraNonce1).slice(0, 4);
                    sendJson({
                        id: message.id,
                        result: [
                            [
                                'mining.notify',
                                String(options.subscriptionId),
                                _this.ethereumStratumVersion
                            ],
                            _this.extraNonce1
                        ],
                        error: null
                    });
                    return;
                }

                // KPSS / RVN kawpow personal stratum (TBM, GMiner, T-Rex): [null, extranonce1]
                _this.extraNonce1 = String(extraNonce1 || '').replace(/^0x/i, '');
                sendJson({
                    id: message.id,
                    result: [
                        null,
                        _this.extraNonce1
                    ],
                    error: null
                });
            });
    }

    function getSafeString(s) {
        return s.toString().replace(/[^a-zA-Z0-9._]+/g, '');
    }

    function getSafeWorkerString(raw) {
        var s = getSafeString(raw).split(".");
        var addr = s[0];
        var wname = "noname";
        if (s.length > 1)
            wname = s[1];
        return addr+"."+wname;
    }

    function handleAuthorize(message){
        // console.log("Handing authorize");

        _this.workerName = getSafeWorkerString(message.params[0]);
        _this.workerPass = message.params[1];

        options.authorizeFn(_this.remoteAddress, options.socket.localPort, _this.workerName, _this.workerPass, _this.extraNonce1, _this.version, function(result) {
            _this.authorized = (!result.error && result.authorized);

            sendJson({
                id     : message.id,
                result : _this.authorized,
                error  : result.error
            });

            _this.emit('authorization');

            // If the authorizer wants us to close the socket lets do it.
            if (result.disconnect === true) {
                options.socket.destroy();
            }
        });
    }

    function handleSubmit(message){
        if (_this.authorized === false){
            sendJson({
                id    : message.id,
                result: null,
                error : [24, "unauthorized worker", null]
            });
            considerBan(false);
            return;
        }
        if (!_this.extraNonce1){
            sendJson({
                id    : message.id,
                result: null,
                error : [25, "not subscribed", null]
            });
            considerBan(false);
            return;
        }

        var submitName = message.params[0];
        var submitJobId = message.params[1];
        var submitNonce = message.params[2];
        var submitHeader = message.params[3];
        var submitMixhash = message.params[4];

        if (_this.ethereumStratum && message.params.length === 3) {
            var minernonce = String(submitNonce || '').replace(/^0x/i, '');
            var extranonce = String(_this.extraNonce1 || '').replace(/^0x/i, '');
            var fullNonce = extranonce + minernonce;
            if (fullNonce.length > 16) {
                fullNonce = fullNonce.slice(-16);
            }
            while (fullNonce.length < 16) {
                fullNonce = '0' + fullNonce;
            }
            submitNonce = '0x' + fullNonce;
            submitHeader = '0x';
            submitMixhash = '0x';
        } else {
            submitNonce = stripHexPrefix(submitNonce);
            submitHeader = stripHexPrefix(submitHeader);
            submitMixhash = stripHexPrefix(submitMixhash);
        }

        _this.emit('submit',
            {
                name        : submitName,
                jobId       : submitJobId,
                jobIdIsShort: _this.ethereumStratum === true,
                nonce       : String(submitNonce).replace(/^0x/i, ''),
                header      : String(submitHeader).replace(/^0x/i, ''),
                mixhash     : String(submitMixhash).replace(/^0x/i, '')
            },
            function(error, result){
                // if (!considerBan(result)){
                    sendJson({
                        id: message.id,
                        result: result,
                        error: error
                    });
                // }
            }
        );

    }

    function handleEthSubmitLogin(message) {
        if (options.coin && options.coin.algorithm === 'kawpow' && !_this.ethereumStratum) {
            sendJson({
                id: message.id,
                jsonrpc: '2.0',
                result: false,
                error: { code: 20, message: 'Unsupported request eth_submitLogin' }
            });
            return;
        }

        _this.protocol = 'eth';
        var login = message.params && message.params.length > 0 ? message.params[0] : '';
        var password = message.params && message.params.length > 1 ? message.params[1] : '';
        var workerValue = login;
        if (message.worker && String(login).indexOf('.') === -1) {
            workerValue = login + '.' + message.worker;
        }
        _this.workerName = getSafeWorkerString(workerValue);
        _this.workerPass = password;

        options.authorizeFn(_this.remoteAddress, options.socket.localPort, _this.workerName, _this.workerPass, _this.extraNonce1, _this.version, function(result) {
            _this.authorized = (!result.error && result.authorized);

            sendJson({
                id: message.id,
                jsonrpc: '2.0',
                result: _this.authorized,
                error: result.error ? { code: -1, message: String(result.error) } : null
            });

            if (_this.authorized) {
                _this.emit('authorization');
                var portDiff = resolveClientDifficulty(_this, options);
                _this.previousDifficulty = _this.difficulty;
                _this.difficulty = portDiff;
            }

            if (result.disconnect === true) {
                options.socket.destroy();
            }
        });
    }

    function handleEthGetWork(message) {
        if (options.coin && options.coin.algorithm === 'kawpow' && !_this.ethereumStratum) {
            sendJson({
                id: message.id,
                jsonrpc: '2.0',
                result: null,
                error: { code: 20, message: 'Unsupported request eth_getWork' }
            });
            return;
        }

        if (_this.authorized !== true) {
            sendJson({
                id: message.id,
                jsonrpc: '2.0',
                result: null,
                error: { code: 23, message: 'Unauthorized worker' }
            });
            return;
        }

        if (typeof options.getJobParams !== 'function') {
            sendJson({
                id: message.id,
                jsonrpc: '2.0',
                result: null,
                error: { code: 0, message: 'Work not ready' }
            });
            return;
        }

        var jobParams = options.getJobParams();
        if (!jobParams || jobParams.length < 7) {
            sendJson({
                id: message.id,
                jsonrpc: '2.0',
                result: null,
                error: { code: 0, message: 'Work not ready' }
            });
            return;
        }

        var work = buildEthWorkParams(jobParams, resolveClientDifficulty(_this, options));
        if (!work) {
            sendJson({
                id: message.id,
                jsonrpc: '2.0',
                result: null,
                error: { code: 0, message: 'Work not ready' }
            });
            return;
        }

        sendJson({
            id: message.id,
            jsonrpc: '2.0',
            result: work,
            error: null
        });
    }

    function handleEthSubmitWork(message) {
        if (options.coin && options.coin.algorithm === 'kawpow' && !_this.ethereumStratum) {
            sendJson({
                id: message.id,
                jsonrpc: '2.0',
                result: false,
                error: { code: 20, message: 'Unsupported request eth_submitWork' }
            });
            return;
        }

        if (_this.authorized === false) {
            sendJson({
                id: message.id,
                jsonrpc: '2.0',
                result: false,
                error: { code: 24, message: 'unauthorized worker' }
            });
            return;
        }

        if (!message.params || message.params.length < 3) {
            sendJson({
                id: message.id,
                jsonrpc: '2.0',
                result: false,
                error: { code: 20, message: 'malformed PoW result' }
            });
            return;
        }

        _this.emit('submitEth',
            {
                name: _this.workerName,
                nonce: stripHexPrefix(message.params[0]),
                header: stripHexPrefix(message.params[1]),
                mixhash: stripHexPrefix(message.params[2])
            },
            function(error, result) {
                sendJson({
                    id: message.id,
                    jsonrpc: '2.0',
                    result: result === true,
                    error: error
                });
            }
        );
    }

    function sendJson(){
        var response = '';
        for (var i = 0; i < arguments.length; i++){
            response += JSON.stringify(arguments[i]) + '\n';
        }
        // Hot path quiet: logging every stratum frame filled multi‑GB logs during KawPow.
        options.socket.write(response);
    }

    function setupSocket(){
        // console.log("Setup socket");
        var socket = options.socket;
        var dataBuffer = '';
        socket.setEncoding('utf8');

        if (options.tcpProxyProtocol === true) {
            socket.once('data', function (d) {
                if (d.indexOf('PROXY') === 0) {
                    _this.remoteAddress = d.split(' ')[2];
                }
                else{
                    _this.emit('tcpProxyError', d);
                }
                _this.emit('checkBan');
            });
        }
        else{
            _this.emit('checkBan');
        }
        socket.on('data', function(d){
            dataBuffer += d;
            if (new Buffer.byteLength(dataBuffer, 'utf8') > 10240){ //10KB
                dataBuffer = '';
                _this.emit('socketFlooded');
                socket.destroy();
                return;
            }
            if (dataBuffer.indexOf('\n') !== -1){
                var messages = dataBuffer.split('\n');
                var incomplete = dataBuffer.slice(-1) === '\n' ? '' : messages.pop();
                messages.forEach(function(message){
                    if (message.length < 1) return;
                    var messageJson;
                    try {
                        messageJson = JSON.parse(message);
                    } catch(e) {
                        if (options.tcpProxyProtocol !== true || d.indexOf('PROXY') !== 0){
                            _this.emit('malformedMessage', message);
                            socket.destroy();
                        }

                        return;
                    }
                    if (messageJson) {
                        // Hot path quiet (see sendJson).
                        handleMessage(messageJson);
                    }
                });
                dataBuffer = incomplete;
            }
        });
        socket.on('close', function() {
            _this.emit('socketDisconnect');
        });
        socket.on('error', function(err){
            if (err.code !== 'ECONNRESET')
                _this.emit('socketError', err);
        });
    }


    this.getLabel = function(){
        return (_this.workerName || '(unauthorized)') + ' [' + _this.remoteAddress + ']';
    };

    this.enqueueNextDifficulty = function(requestedNewDifficulty) {
        pendingDifficulty = requestedNewDifficulty;
        return true;
    };

    //public members

    /**
     * IF the given difficulty is valid and new it'll send it to the client.
     * returns boolean
     **/
    this.sendDifficulty = function(difficulty){
        if (_this.authorized !== true) {
            return false;
        }
        if (difficulty === this.difficulty)
            return false;
        _this.previousDifficulty = _this.difficulty;
        _this.difficulty = difficulty;

        if (_this.protocol === 'eth') {
            return true;
        }

        if (_this.ethereumStratum) {
            sendJson({
                id    : null,
                method: "mining.set_difficulty",
                params: [difficulty]
            });
            return true;
        }

        // KPSS kawpow uses mining.set_target (not set_difficulty).
        sendJson({
            id    : null,
            method: "mining.set_target",
            params: [util.kawpowShareTargetHex(difficulty)]
        });

        return true;
    };

    this.sendExtranonce = function() {
        if (_this.authorized !== true || _this.protocol === 'eth' || !_this.extraNonce1) {
            return false;
        }

        sendJson({
            id    : null,
            method: "mining.set_extranonce",
            params: [_this.extraNonce1]
        });

        return true;
    };

    /** Keepalive during idle windows (matches ckpool mining.ping; no work issued). */
    this.sendPing = function(){
        if (_this.authorized !== true) {
            return false;
        }
        if (_this.protocol === 'eth') {
            return false;
        }
        _this.lastActivity = Date.now();
        sendJson({
            id    : 42,
            method: 'mining.ping',
            params: []
        });
        return true;
    };

    this.sendMiningJob = function(jobParams){

        var lastActivityAgo = Date.now() - _this.lastActivity;
        if (lastActivityAgo > options.connectionTimeout * 1000){
            _this.socket.destroy();
            return;
        }

        if (_this.authorized !== true) {
            return;
        }

        if (_this.protocol === 'eth') {
            // eth_getWork clients are expected to poll for work. Unsolicited
            // "result" payloads with id 0 can confuse TBM's state machine.
            return;
        }

        if (pendingDifficulty !== null){
            var result = _this.sendDifficulty(pendingDifficulty);
            pendingDifficulty = null;
            if (result) {
                _this.emit('difficultyChanged', _this.difficulty);
            }
        }

        //change target to miners' personal varDiff target
        var personal_jobParams = jobParams.slice();
        personal_jobParams[3] = util.kawpowShareTargetHex(resolveClientDifficulty(_this, options));

        if (_this.ethereumStratum) {
            // EthereumStratum/1.0.0 (TeamRedMiner, etc.): jobId, seedhash, headerhash, clean.
            // NiceHash spec order — seed first, then header (not the 7-param kawpow order).
            sendJson({
                id    : null,
                method: "mining.notify",
                params: [
                    shortJobIdFromParams(personal_jobParams),
                    personal_jobParams[2],
                    personal_jobParams[1],
                    personal_jobParams[4] === true
                ]
            });
            return;
        }

        // Standard kawpow: full 7-param job (jobId, header, seed, target, clean, height, bits)
        sendJson({
            id    : null,
            method: "mining.notify",
            params: personal_jobParams
        });

    };

    this.manuallyAuthClient = function (username, password) {
        handleAuthorize({id: 1, params: [username, password]}, false /*do not reply to miner*/);
    };

    this.manuallySetValues = function (otherClient) {
        _this.extraNonce1        = otherClient.extraNonce1;
        _this.previousDifficulty = otherClient.previousDifficulty;
        _this.difficulty         = otherClient.difficulty;
    };
};
StratumClient.prototype.__proto__ = events.EventEmitter.prototype;




/**
 * The actual stratum server.
 * It emits the following Events:
 *   - 'client.connected'(StratumClientInstance) - when a new miner connects
 *   - 'client.disconnected'(StratumClientInstance) - when a miner disconnects. Be aware that the socket cannot be used anymore.
 *   - 'started' - when the server is up and running
 **/
var StratumServer = exports.Server = function StratumServer(options, authorizeFn){

    //private members

    //ports, connectionTimeout, jobRebroadcastTimeout, banning, haproxy, authorizeFn

    var bannedMS = options.banning ? options.banning.time * 1000 : null;

    var _this = this;
    var stratumClients = {};
    var subscriptionCounter = SubscriptionCounter();
    var rebroadcastTimeout;
    var bannedIPs = {};

    function checkBan(client){
        if (options.banning && options.banning.enabled && client.remoteAddress in bannedIPs){
            var bannedTime = bannedIPs[client.remoteAddress];
            var bannedTimeAgo = Date.now() - bannedTime;
            var timeLeft = bannedMS - bannedTimeAgo;
            if (timeLeft > 0){
                client.socket.destroy();
                client.emit('kickedBannedIP', timeLeft / 1000 | 0);
            }
            else {
                delete bannedIPs[client.remoteAddress];
                client.emit('forgaveBannedIP');
            }
        }
    }

    this.handleNewClient = function (socket){

        // console.log("Handling new client");

        socket.setKeepAlive(true);
        var subscriptionId = subscriptionCounter.next();
        var client = new StratumClient(
            {
                coin: options.coin,
                subscriptionId: subscriptionId,
                authorizeFn: authorizeFn,
                socket: socket,
                banning: options.banning,
                connectionTimeout: options.connectionTimeout,
                tcpProxyProtocol: options.tcpProxyProtocol,
                portsConfig: options.ports,
                getJobParams: options.getJobParams
            }
        );

        stratumClients[subscriptionId] = client;
        _this.emit('client.connected', client);
        client.on('socketDisconnect', function() {
            _this.removeStratumClientBySubId(subscriptionId);
            _this.emit('client.disconnected', client);
        }).on('checkBan', function(){
            checkBan(client);
        }).on('triggerBan', function(){
            _this.addBannedIP(client.remoteAddress);
        }).init();
        return subscriptionId;
    };


    this.broadcastMiningJobs = function(jobParams){
        for (var clientId in stratumClients) {
            var client = stratumClients[clientId];
            client.sendMiningJob(jobParams);
        }
        /* Some miners will consider the pool dead if it doesn't receive a job for around a minute.
           So every time we broadcast jobs, set a timeout to rebroadcast in X seconds unless cleared. */
        clearTimeout(rebroadcastTimeout);
        rebroadcastTimeout = setTimeout(function(){
            _this.emit('broadcastTimeout');
        }, options.jobRebroadcastTimeout * 1000);
    };

    this.clearJobRebroadcast = function(){
        clearTimeout(rebroadcastTimeout);
        rebroadcastTimeout = null;
    };

    /** Broadcast mining.ping to authorized KawPow clients (SHA-window idle keepalive). */
    this.broadcastPing = function(){
        for (var clientId in stratumClients) {
            var client = stratumClients[clientId];
            client.sendPing();
        }
    };



    (function init(){

        //Interval to look through bannedIPs for old bans and remove them in order to prevent a memory leak
        if (options.banning && options.banning.enabled){
            setInterval(function(){
                for (ip in bannedIPs){
                    var banTime = bannedIPs[ip];
                    if (Date.now() - banTime > options.banning.time)
                        delete bannedIPs[ip];
                }
            }, 1000 * options.banning.purgeInterval);
        }

        var serversStarted = 0;
        Object.keys(options.ports).forEach(function(port){
            if (options.ports[port].tls) {
                // console.log("TLS port for "+port);
                tls.createServer(TLSoptions, function(socket) {
                    _this.handleNewClient(socket);
                }).listen(parseInt(port), function() {
                    serversStarted++;
                    if (serversStarted == Object.keys(options.ports).length)
                        _this.emit('started');
                });
            } else {
              // console.log("TCP port for "+port);
              net.createServer({allowHalfOpen: false}, function(socket) {
                  _this.handleNewClient(socket);
              }).listen(parseInt(port), function() {
                  serversStarted++;
                  if (serversStarted == Object.keys(options.ports).length)
                      _this.emit('started');
              });
            }
        });
    })();



    //public members

    this.addBannedIP = function(ipAddress){
        bannedIPs[ipAddress] = Date.now();
        /*for (var c in stratumClients){
            var client = stratumClients[c];
            if (client.remoteAddress === ipAddress){
                _this.emit('bootedBannedWorker');
            }
        }*/
    };

    this.getStratumClients = function () {
        return stratumClients;
    };

    this.removeStratumClientBySubId = function (subscriptionId) {
        delete stratumClients[subscriptionId];
    };

    this.manuallyAddStratumClient = function(clientObj) {
        var subId = _this.handleNewClient(clientObj.socket);
        if (subId != null) { // not banned!
            stratumClients[subId].manuallyAuthClient(clientObj.workerName, clientObj.workerPass);
            stratumClients[subId].manuallySetValues(clientObj);
        }
    };

};
StratumServer.prototype.__proto__ = events.EventEmitter.prototype;
