const EventEmitter = require('events').EventEmitter;
const MessagePortMain = require('./message-port-main.js');

const binding = process._linkedBinding('electron_browser_web_frame_main');
const WebFrameMain = binding.WebFrameMain;
const fromId = binding.fromId;
const fromIdOrNull = binding.fromIdOrNull;

Object.setPrototypeOf(WebFrameMain.prototype, EventEmitter.prototype);

WebFrameMain.prototype.send = function(channel, ...args) {
    if (typeof channel !== 'string') {
        throw new TypeError('Missing required channel argument');
    }

    try {
        return this._send(false, channel, ...args);
    } catch (e) {
        console.error('Error sending from webFrameMain: ', e);
    }
};

WebFrameMain.prototype._sendInternal = function(channel, ...args) {
    if (typeof channel !== 'string') {
        throw new TypeError('Missing required channel argument');
    }

    try {
        return this._send(true, channel, ...args);
    } catch (e) {
        console.error('Error sending from webFrameMain: ', e);
    }
};

WebFrameMain.prototype.postMessage = function(...args) {
    if (Array.isArray(args[2])) {
        args[2] = args[2].map(o => o instanceof MessagePortMain ? o._internalPort : o);
    }
    this._postMessage(...args);
};

module.exports = {
    WebFrameMain,
    webFrameMain: {
        fromId,
        fromIdOrNull
    }
};
