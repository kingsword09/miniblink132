
const binding = process._linkedBinding('electron_browser_protocol');
const Protocol = binding.Protocol;
const protocol = new Protocol(onLoadUrlBegin);

var handlerToIdMap = {};
var schemeToIdMap = {};
var idGen = 0;

function onLoadUrlBegin(id, request, nativeCallbackInfo) {
    var handler = handlerToIdMap[id];
    //mbConsoleLog("Protocol.onLoadUrlBegin:" + handler);
    if (!handler) {
        protocol.onHandlerFinish(request, nativeCallbackInfo);
        return;
    }

    handler(request, function(request) {
//        var filePath;
//        if ("string" != (typeof redirectRequest)) {
//            filePath = redirectRequest.path;
//        }
//        if ("string" != (typeof filePathTrim))
//            return;
        
        protocol.onHandlerFinish(request, nativeCallbackInfo);
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

Protocol.prototype.uninterceptProtocol = function(scheme, completion) {
    this.unregisterProtocol(scheme, completion);
};

[
    "registerProtocol",
    "registerFileProtocol",
    "registerBufferProtocol",
    "registerStringProtocol",
    "registerHttpProtocol",
    "unregisterProtocol",
    "isProtocolHandled",
    "interceptFileProtocol",
    "interceptStringProtocol",
    "interceptBufferProtocol",
    "interceptHttpProtocol",
    "uninterceptProtocol"
].forEach(function(name) {
    protocol[name] = Protocol.prototype[name];
});

exports.protocol = protocol;
