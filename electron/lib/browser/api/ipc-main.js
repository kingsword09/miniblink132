const EventEmitter = require('events').EventEmitter;
const log = typeof mbConsoleLog === 'function' ? mbConsoleLog : function() {};

function createIpcMain() {
    var ipcMain = new EventEmitter();
    ipcMain.m_invokeHandlers = new Map();
    
    ipcMain.__origOn__ = ipcMain.on;
    ipcMain.on = function (channel, callback) {
        let guardedCallback = function (event, ...args) {
            if (event && event.innnerChannel == 'ipc-render-invoke')
                return;
            callback(event, ...args);
        }
        return ipcMain.__origOn__(channel, guardedCallback);
    }
    
    ipcMain.handle = function (channel, listener) { // 这里的channel是用户定义的channel
        if (ipcMain.m_invokeHandlers.has(channel)) {
            throw new Error("Attempted to register a second handler for " + channel);
        }
        if (typeof listener !== 'function') {
            throw new TypeError("Expected handler to be a function, but found type " + (typeof listener));
        }
        
        const invokeHandler = function (event, ...args) {
            // The channel is already the EventEmitter event name here.
            log("ipcMain.handle.on::" + channel);
            if (event.innnerChannel != 'ipc-render-invoke')
                return;
            
            var promiseOrResult = listener(/*channel,*/ event, ...args);
            Promise.resolve(promiseOrResult).then(function(result) {
                log("ipcMain.on handle result!" + result);
                event.sender.send('ipc-main-handle-reply-' + channel, result);
            });
        };
        ipcMain.m_invokeHandlers.set(channel, invokeHandler);
        ipcMain.__origOn__(channel, invokeHandler);
    }
    
    ipcMain.removeHandler = function (channel /*string*/) {
        if (ipcMain.m_invokeHandlers.has(channel)) {
            let invokeHandler = ipcMain.m_invokeHandlers.get(channel);
            ipcMain.removeListener(channel, invokeHandler);
            ipcMain.m_invokeHandlers.delete(channel);
        }
    }
    
    return ipcMain;
}

var ipcMain = createIpcMain();
ipcMain.createIpcMain = createIpcMain;

module.exports = ipcMain;

// Do not throw exception when channel name is "error".
module.exports.on('error', () => undefined)
