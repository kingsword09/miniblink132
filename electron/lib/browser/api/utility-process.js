const EventEmitter = require('events').EventEmitter;
const MessagePortMain = require('./message-port-main.js');
const { PassThrough } = require('stream');
const binding = process._linkedBinding('electron_browser_utility_process');
const _fork = binding._fork;

function ForkUtilityProcess (modulePath/*: string*/, args/*?: string[]*/, options/*?: Electron.ForkOptions*/) {
    if (!modulePath)
        throw new Error('Missing UtilityProcess entry script.');
    if (args != null && !Array.isArray(args)) {
        options = args;
        args = [];
    } else if (args == null) {
        args = [];
    }
    if (!args.every(arg => typeof arg === 'string'))
        throw new TypeError('args must be an array of strings.');
    if (options == null) {
        options = {};
    } else {
        options = { ...options };
    }
    if (options.execArgv != null) {
        if (!Array.isArray(options.execArgv)) {
            throw new TypeError('execArgv must be an array of strings.');
        }
    }

    if (options.serviceName != null) {
        if (typeof options.serviceName !== 'string') {
            throw new TypeError('serviceName must be a string.');
        }
    }

    if (options.cwd != null) {
        if (typeof options.cwd !== 'string') {
            throw new TypeError('cwd path must be a string.');
        }
    }

    // O:\chromium\ele32fp\electron\lib\browser\api\utility-process.ts
    if (typeof options.stdio === 'string') {
        let stdio /*: Array<'pipe' | 'ignore' | 'inherit'>*/ = [];
        switch (options.stdio) {
            case 'inherit':
            case 'ignore':
                stdio.push('ignore', options.stdio, options.stdio);
                break;
            case 'pipe':
                this._stderr = new PassThrough();
                this._stdout = new PassThrough();
                stdio.push('ignore', options.stdio, options.stdio);
                break;
            default:
                throw new Error('stdio must be of the following values: inherit, pipe, ignore');
        }
        options.stdio = stdio;
    } else if (Array.isArray(options.stdio)) {
        if (options.stdio.length >= 3) {
            if (options.stdio[0] !== 'ignore') {
                throw new Error('stdin value other than ignore is not supported.');
            }

            if (options.stdio[1] === 'pipe') {
                this._stdout = new PassThrough();
            } else if (options.stdio[1] !== 'ignore' && options.stdio[1] !== 'inherit') {
                throw new Error('stdout configuration must be of the following values: inherit, pipe, ignore');
            }

            if (options.stdio[2] === 'pipe') {
                this._stderr = new PassThrough();
            } else if (options.stdio[2] !== 'ignore' && options.stdio[2] !== 'inherit') {
                throw new Error('stderr configuration must be of the following values: inherit, pipe, ignore');
            }
        } else {
            throw new Error('configuration missing for stdin, stdout or stderr.');
        }
    }

    this._handle = _fork({ options, modulePath, args });
    if (!this._handle)
        throw new Error('ForkUtilityProcess _fork fail.');

    this._handle.emit = (channel/*: string | symbol*/, ...args/*: any[]*/) => {
        if (channel === 'exit') {
            try {
                this.emit('exit', ...args);
            } finally {
                this._handle = null;
                if (this._stdout) {
                    this._stdout.removeAllListeners();
                    this._stdout = null;
                }
                if (this._stderr) {
                    this._stderr.removeAllListeners();
                    this._stderr = null;
                }
            }
            return false;
        } else {
            return this.emit(channel, ...args);
        }
    } // emit end
}

Object.setPrototypeOf(ForkUtilityProcess.prototype, EventEmitter.prototype);

Object.defineProperty(ForkUtilityProcess.prototype, 'pid', {
    get: function() {
        if (!this._handle)
            return 0;
        return typeof this._handle.pid === 'function' ? this._handle.pid() : this._handle.pid;
    }
});

Object.defineProperty(ForkUtilityProcess.prototype, 'stdout', {
    get: function() {
        return this._stdout || null;
    }
});

Object.defineProperty(ForkUtilityProcess.prototype, 'stderr', {
    get: function() {
        return this._stderr || null;
    }
});

ForkUtilityProcess.prototype.postMessage = function(message/*: any*/, transfer /*?: MessagePortMain[]*/) {
    if (!this._handle)
        return ;
    if (Array.isArray(transfer)) {
        transfer = transfer.map((o/*: any*/) => o instanceof MessagePortMain ? o._internalPort : o);
        return this._handle?.postMessage(message, transfer);
    }
    return this._handle.postMessage(message);
}

/*boolean*/
ForkUtilityProcess.prototype.kill = function() {
    if (this._handle === null) {
      return false;
    }
    return this._handle.kill();
}

function fork(modulePath/*: string*/, args/*?: string[]*/, options/*?: Electron.ForkOptions*/) {
    return new ForkUtilityProcess(modulePath, args, options);
}

module.exports = {
    "utilityProcess": {"fork": fork}
};
