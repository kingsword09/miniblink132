
const { Buffer } = require('buffer');
const binding = process._linkedBinding('electron_browser_protocol');
const Protocol = binding.Protocol;
const protocol = new Protocol(onLoadUrlBegin);

var handlerToIdMap = {};
var schemeToIdMap = {};
var idGen = 0;

function finishHandlerResponse(response, nativeCallbackInfo) {
    protocol.onHandlerFinish(response, nativeCallbackInfo);
}

function onLoadUrlBegin(id, request, nativeCallbackInfo) {
    var handler = handlerToIdMap[id];
    //mbConsoleLog("Protocol.onLoadUrlBegin:" + handler);
    if (!handler) {
        finishHandlerResponse(request, nativeCallbackInfo);
        return;
    }

    handler(request, function(request) {
//        var filePath;
//        if ("string" != (typeof redirectRequest)) {
//            filePath = redirectRequest.path;
//        }
//        if ("string" != (typeof filePathTrim))
//            return;
        
        finishHandlerResponse(request, nativeCallbackInfo);
    });
}

Protocol.prototype.registerProtocol = function(scheme, handler, completion, type) {
    var id = ++idGen;
    handlerToIdMap[id] = handler;
    var registered = this._registerProtocol(
        scheme,
        id,
        type,
        globalThis.__electronProtocolSkipBlinkRegistrationForTesting ? false : undefined);
    if (registered)
        schemeToIdMap[scheme] = id;
    else
        delete handlerToIdMap[id];
    if (completion)
        completion(registered ? null : new Error('The scheme has been registered'));
}

Protocol.prototype.registerFileProtocol = function(scheme, handler, completion) {
    this.registerProtocol(scheme, handler, completion, "file");
}

Protocol.prototype.registerBufferProtocol = function(scheme, handler, completion) {
    this.registerProtocol(scheme, handler, completion, "buffer");
}

Protocol.prototype.registerStringProtocol = function(scheme, handler, completion) {
    this.registerProtocol(scheme, handler, completion, "string");
}

Protocol.prototype.registerHttpProtocol = function(scheme, handler, completion) {
    this.registerProtocol(scheme, handler, completion, "http");
}

function collectStream(stream, callback) {
    if (!stream || typeof stream.on !== 'function')
        throw new TypeError('stream protocol handler must pass a readable stream');

    const chunks = [];
    stream.on('data', function(chunk) {
        if (typeof chunk === 'string')
            chunks.push(Buffer.from(chunk));
        else if (Buffer.isBuffer(chunk))
            chunks.push(chunk);
        else if (chunk instanceof Uint8Array)
            chunks.push(Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength));
        else
            stream.destroy(new TypeError('stream protocol emitted unsupported chunk type'));
    });
    stream.once('error', function() {
        callback(null);
    });
    stream.once('end', function() {
        callback(Buffer.concat(chunks));
    });
}

function wrapStreamHandler(handler) {
    if (typeof handler !== 'function')
        throw new TypeError('handler must be a function');

    return function(request, callback) {
        handler(request, function(response) {
            if (!response)
                return callback(null);

            const stream = response.data || response;
            collectStream(stream, function(buffer) {
                if (!buffer)
                    return callback(null);
                callback({
                    data: buffer,
                    mimeType: response.mimeType || 'application/octet-stream'
                });
            });
        });
    };
}

Protocol.prototype.registerStreamProtocol = function(scheme, handler, completion) {
    this.registerProtocol(scheme, wrapStreamHandler(handler), completion, "buffer");
}

Protocol.prototype.unregisterProtocol = function(scheme, completion) {
    var id = schemeToIdMap[scheme];
    if (id !== undefined)
        delete handlerToIdMap[id];
    delete schemeToIdMap[scheme];

    this._unregisterProtocol(scheme);
    if (completion)
        completion(null);
}

Protocol.prototype.isProtocolHandled = function(scheme, callback) {
    var b = this._isProtocolHandled(scheme);
    callback(b);
}

Protocol.prototype.interceptFileProtocol = function(scheme, handler, completion) {
    this.registerProtocol(scheme, handler, completion, "file");
};

Protocol.prototype.interceptStringProtocol = function(scheme, handler, completion) {
    this.registerProtocol(scheme, handler, completion, "string");
};

Protocol.prototype.interceptBufferProtocol = function(scheme, handler, completion) {
    this.registerProtocol(scheme, handler, completion, "buffer");
};

Protocol.prototype.interceptHttpProtocol = function(scheme, handler, completion) {
    this.registerProtocol(scheme, handler, completion, "http");
};

Protocol.prototype.interceptStreamProtocol = function(scheme, handler, completion) {
    this.registerProtocol(scheme, wrapStreamHandler(handler), completion, "buffer");
};

Protocol.prototype.uninterceptProtocol = function(scheme, completion) {
    this.unregisterProtocol(scheme, completion);
};

[
    "registerProtocol",
    "registerFileProtocol",
    "registerBufferProtocol",
    "registerStringProtocol",
    "registerHttpProtocol",
    "registerStreamProtocol",
    "unregisterProtocol",
    "isProtocolHandled",
    "interceptFileProtocol",
    "interceptStringProtocol",
    "interceptBufferProtocol",
    "interceptHttpProtocol",
    "interceptStreamProtocol",
    "uninterceptProtocol",
    "registerStandardSchemes",
    "registerSchemesAsPrivileged"
].forEach(function(name) {
    var method = Protocol.prototype[name];
    if (typeof method === "function")
        protocol[name] = method;
});

exports.protocol = protocol;
