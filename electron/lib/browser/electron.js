
//process._linkedBinding('electron');
const electron = require('electron');
const electronMain = require('electron/main');
//const electron = process._linkedBinding('electron_browser_electron');

const ipcMain = require('./api/ipc-main');
electron.ipcMain = ipcMain;

var App = require("./../browser/api/app").App;
electron.app = new App();

electron.BrowserWindow = require("./../browser/api/browser-window.js");
electron.BrowserView = require("./../browser/api/browser-view.js");
electron.webContents = require("./../browser/api/web-contents.js");
electron.session = require("./../browser/api/session.js").session;
electron.app.commandLine = require("./../browser/api/command-line.js");

var s_map = {};
var s_idGen = 0;

electron.webContents.prototype.onCreateNewWebview = function(event, urlOrOpt) {
    let win;
    if (typeof urlOrOpt == "string")
        win = new electron.BrowserWindow({width: 800, height: 600, show: true});
    else
        win = new electron.BrowserWindow(urlOrOpt);
    win.show();
    
    var id = ++s_idGen;
    s_map[id] = win;
    win.webContents.once("destroyed", function() { console.log('onCreateNewWebview, destroy'); delete s_map[id]; });
    console.log("electron.webContents.prototype.onCreateNewWebview!!!!!!!!!!!");
    event.newGuest = win;
}

const EventEmitter = require('events').EventEmitter;
Object.setPrototypeOf(App.prototype, EventEmitter.prototype);

let orgiOnFn = App.prototype.on;
App.prototype.on = function(evt, cb) {
    if ("ready" == evt && this.isReady()) {
        cb();
        return;
    }
    orgiOnFn.call(this, evt, cb);
}

App.prototype.enableSandbox = function() {}
const nativeGetLocale = App.prototype.getLocale;
App.prototype.getLocale = function() {
    return nativeGetLocale.call(this) || 'en-US';
}
App.prototype.getSystemLocale = function() {
    return this.getLocale();
}
App.prototype.getPreferredSystemLanguages = function() {
    const locale = this.getLocale();
    return locale ? [locale] : ['en-US'];
}

const MenuItem = require('./api/menu-item.js');
electron.MenuItem = MenuItem;

const Menu = require('./api/menu.js');
electron.Menu = Menu;

const isPromise = require('./../common/api/is-promise.js').isPromise;
electron.isPromise = isPromise;

const dialog = require('./api/dialog.js').dialog;
electron.dialog = dialog;

const net = require('./api/net.js').net;
electron.net = net;

electron.utilityProcess = require('./api/utility-process.js').utilityProcess;
require('./api/parent-port.js');
electron.webFrameMain = require('./api/web-frame-main.js').webFrameMain;

electron.MessageChannelMain = require('./api/message-channel-main.js').MessageChannelMain;

electron.shell = require("./../common/api/shell.js").Shell;
electron.screen = require("./../common/api/screen.js").Screen;
electron.tray = require("./../common/api/screen.js").Tray;
electron.clipboard = require("./../common/api/clipboard.js");
electron.nativeImage = require("./../common/api/native-image.js").NativeImage;
electron.safeStorage = require("./api/safe-storage.js");
const nativeThemeState = function() {
    const source = process.env.MINIBLINK_NATIVE_THEME || 'system';
    const highContrast = process.env.MINIBLINK_HIGH_CONTRAST === '1';
    const dark = source === 'dark';
    return { source, highContrast, dark };
}
electron.nativeTheme = {};
Object.defineProperties(electron.nativeTheme, {
    shouldUseDarkColors: { get: function() { return nativeThemeState().dark; } },
    shouldUseHighContrastColors: { get: function() { return nativeThemeState().highContrast; } },
    shouldUseInvertedColorScheme: { get: function() { return false; } },
    themeSource: {
        get: function() { return nativeThemeState().source; },
        set: function(value) { process.env.MINIBLINK_NATIVE_THEME = value || 'system'; }
    }
});
electron.nativeTheme.on = function() { return this; }
electron.nativeTheme.removeListener = function() { return this; }
    
function SystemPreferences () {}
SystemPreferences.prototype.isDarkMode = function() { return electron.nativeTheme.shouldUseDarkColors; }
SystemPreferences.prototype.isSwipeTrackingFromScrollEventsEnabled = function() { return false; }
SystemPreferences.prototype.isAeroGlassEnabled = function() { return false; }
SystemPreferences.prototype.isInvertedColorScheme = function() { return electron.nativeTheme.shouldUseInvertedColorScheme; }
Object.setPrototypeOf(SystemPreferences.prototype, EventEmitter.prototype);
electron.systemPreferences = new SystemPreferences();

////////////////////////////////////////////////////////////////

electron.protocol = require("./api/protocol.js").protocol;

// function Protocol() {}
// Protocol.prototype.registerStandardSchemes = function(schemes) {}
// Protocol.prototype.registerServiceWorkerSchemes = function(scheme) {}
// Protocol.prototype.registerFileProtocol = function(scheme, handler, completion) {}
// Protocol.prototype.registerBufferProtocol = function(scheme, handler, completion) {}
// Protocol.prototype.registerStringProtocol = function(scheme, handler, completion) {}
// Protocol.prototype.registerHttpProtocol = function(scheme, handler, completion) {}
// Protocol.prototype.unregisterProtocol = function(scheme, completion) {}
// Protocol.prototype.isProtocolHandled = function(scheme, callback) {}
// Protocol.prototype.interceptFileProtocol = function(scheme, handler, completion) {}
// Protocol.prototype.interceptStringProtocol = function(scheme, handler, completion) {}
// Protocol.prototype.interceptBufferProtocol = function(scheme, handler, completion) {}
// Protocol.prototype.interceptHttpProtocol = function(scheme, handler, completion) {}
// Protocol.prototype.uninterceptProtocol = function(scheme, completion) {}

// electron.protocol = new Protocol();
////////////////////////////////////////////////////////////////
function ContentTracing() {}

ContentTracing.prototype.startRecording = function() {
    return new Promise(function(resolveOuter) {
        setTimeout(function() {
            resolveOuter();
        }, 1000);
    });
}

ContentTracing.prototype.stopRecording = function() {
    return new Promise(function(resolveOuter) {
        setTimeout(function() {
            resolveOuter("");
        }, 1000);
    });
}

ContentTracing.prototype.getTraceBufferUsage = function() {
    return new Promise(function(resolveOuter) {
        setTimeout(function() {
            resolveOuter({"value":1, "percentage":1});
        }, 1000);
    });
}
electron.contentTracing = new ContentTracing();
////////////////////////////////////////////////////////////////
var TouchBar = function (){};
TouchBar.TouchBarButton = function() {};
TouchBar.TouchBarColorPicker = function() {};
TouchBar.TouchBarGroup = function() {};
TouchBar.TouchBarLabel = function() {};
TouchBar.TouchBarPopover = function() {};
TouchBar.TouchBarScrubber = function() {};
TouchBar.TouchBarSegmentedControl = function() {};
TouchBar.TouchBarSlider = function() {};
TouchBar.TouchBarSpacer = function() {};

electron.TouchBar = TouchBar;

////////////////////////////////////////////////////////////////

function AutoUpdater() {}
Object.setPrototypeOf(AutoUpdater.prototype, EventEmitter.prototype);
AutoUpdater.prototype.on = function(evt, callback) {}
AutoUpdater.prototype.setFeedURL= function(url) {}
AutoUpdater.prototype.checkForUpdates = function() {}
AutoUpdater.prototype.quitAndInstall = function() {}

electron.autoUpdater = new AutoUpdater();
////////////////////////////////////////////////////////////////
/*
function Tray() {}
Tray.prototype.on = function(evt, callback) {}
Tray.prototype.destroy = function() {}
Tray.prototype.setImage= function(image) {}
Tray.prototype.setPressedImage = function(image) {}
Tray.prototype.setToolTip = function(toolTip) {}
Tray.prototype.setTitle = function(title) {}
Tray.prototype.displayBalloon = function(options) {}
Tray.prototype.popUpContextMenu = function(menu, position) {}
Tray.prototype.setContextMenu = function(menu) {}

electron.Tray = Tray;
*/
electron.Tray = require("./api/tray").Tray;
////////////////////////////////////////////////////////////////
function GlobalShortcut() {}
GlobalShortcut.prototype.register = function(accelerator, callback) {}
GlobalShortcut.prototype.isRegistered = function(accelerator) { return true; }
GlobalShortcut.prototype.unregister= function() {}
GlobalShortcut.prototype.unregisterAll = function() {}
electron.globalShortcut = new GlobalShortcut();
////////////////////////////////////////////////////////////////
//function PowerMonitor() {}
//Object.setPrototypeOf(PowerMonitor.prototype, EventEmitter.prototype);
//PowerMonitor.prototype.on = function(evtName, callback) {}
//PowerMonitor.prototype.removeListener = function(evtName) {}
//electron.powerMonitor = new PowerMonitor();
electron.powerMonitor = require("./api/power-monitor");
////////////////////////////////////////////////////////////////
electron.powerSaveBlocker = require("./api/power-save-blocker");
////////////////////////////////////////////////////////////////
function CrashReporter () {
    this.m_isUploadToServer = true;
}
CrashReporter.prototype.start = function(options) { return; }
CrashReporter.prototype.getLastCrashReport = function() { return null; }
CrashReporter.prototype.getUploadedReports = function() { return 0; }
CrashReporter.prototype.getUploadToServer = function() { return m_isUploadToServer; }
CrashReporter.prototype.setUploadToServer = function(uploadToServer) { m_isUploadToServer = uploadToServer; }
CrashReporter.prototype.addExtraParameter = function(key, value) { return 0; }
CrashReporter.prototype.removeExtraParameter = function(key) { return 0; }
CrashReporter.prototype.getParameters = function(key) { return {}; }
electron.crashReporter = new CrashReporter();
////////////////////////////////////////////////////////////////

Object.assign(electronMain, electron);
module.exports = electron;

