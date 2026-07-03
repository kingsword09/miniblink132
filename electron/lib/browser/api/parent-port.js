const EventEmitter = require('events').EventEmitter;
const MessagePortMain = require('./message-port-main.js');
const binding = process._linkedBinding('electron_browser_parent_port');
const ParentPort = binding.ParentPort;

if (ParentPort) {
    Object.setPrototypeOf(ParentPort.prototype, EventEmitter.prototype);

    ParentPort.prototype.postMessage = function(message) {
        return this._send(message);
    };

    const origEmit = ParentPort.prototype.emit;
    ParentPort.prototype.emit = function(channel/*: string | symbol*/, event/*: { ports: any[] }*/) {
        if (channel === 'message') {
            const ports = event && Array.isArray(event.ports) ? event.ports : [];
            event = { ...event, ports: ports.map(p => new MessagePortMain(p)) };
        }
        origEmit.call(this, channel, event);
        return false;
    };

    process.parentPort = new ParentPort(true);
}
