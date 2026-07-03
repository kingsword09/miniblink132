
process._linkedBinding('electron_browser_web_contents');

const EventEmitter = require('events').EventEmitter;
const TouchBar = require('./touch-bar');
const BrowserWindow = process._linkedBinding('electron_browser_browserwindow').BrowserWindow;
Object.setPrototypeOf(BrowserWindow.prototype, EventEmitter.prototype);

require('./web-contents');

// Helpers.
Object.defineProperty(BrowserWindow.prototype, "webContents", {
    get: function () {
        var webContents =  this._getWebContents();
        webContents._init();
        return webContents;
    },
    configurable : true
});

BrowserWindow.prototype.setTouchBar = function(touchBar) {
    if (touchBar == null) {
        this._touchBar = null;
        if (typeof this._setTouchBar === 'function')
            this._setTouchBar(null);
        return undefined;
    }

    if (!(touchBar instanceof TouchBar))
        touchBar = new TouchBar(touchBar);

    this._touchBar = touchBar;
    if (typeof this._setTouchBar === 'function')
        this._setTouchBar(touchBar._serialize());
    return undefined;
}

BrowserWindow.prototype._dispatchTouchBarAction = function(action) {
    if (!this._touchBar || typeof this._touchBar._handleActionFromNative !== 'function')
        return false;
    return this._touchBar._handleActionFromNative(action);
}

BrowserWindow.prototype.setTitle = function(str) {
    if (typeof(str) == "string")
        this._setTitle(str);
}

Object.assign(BrowserWindow.prototype, {
    loadURL (...args) {
        var self = this;
        var result = new Promise(function(resolve, reject) {
            self.webContents.once("did-finish-load", function() {
                resolve();
            });
            self.webContents.once("did-fail-load", function() {
                reject(new Error("Failed to load URL"));
            });

            self.webContents._loadURL.apply(self.webContents, args);
        });
        return result;
    },
    loadFile (...args) {
        var self = this;
        var result = new Promise(function(resolve, reject){
            self.webContents.once("did-finish-load", function() {
                resolve();
            });
            self.webContents.once("did-fail-load", function() {
                reject(new Error("Failed to load file"));
            });
            self.webContents.loadFile.apply(self.webContents, args);
        });
        return result;
    },
    getURL (...args) {
        return this.webContents.getURL();
    },
    reload (...args) {
        return this.webContents.reload.apply(this.webContents, args);
    },
    send (...args) {
        return this.webContents.send.apply(this.webContents, args);
    },
    openDevTools (...args) {
        return this.webContents.openDevTools.apply(this.webContents, args);
    },
    closeDevTools () {
        return this.webContents.closeDevTools();
    },
    isDevToolsOpened () {
        return this.webContents.isDevToolsOpened();
    },
    isDevToolsFocused () {
        return this.webContents.isDevToolsFocused();
    },
    toggleDevTools () {
        return this.webContents.toggleDevTools();
    },
    inspectElement (...args) {
        return this.webContents.inspectElement.apply(this.webContents, args);
    },
    inspectServiceWorker () {
        return this.webContents.inspectServiceWorker();
    },
    showDefinitionForSelection () {
        return this.webContents.showDefinitionForSelection();
    },
    capturePage (...args) {
        return this.webContents.capturePage.apply(this.webContents, args);
    }

});

module.exports = BrowserWindow;
