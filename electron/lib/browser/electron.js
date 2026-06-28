'use strict';

const electron = require('electron');
const electronMain = require('electron/main');
const EventEmitter = require('events').EventEmitter;

const App = require('./api/app').App;
const app = new App();
Object.setPrototypeOf(App.prototype, EventEmitter.prototype);

const nativeOn = App.prototype.on;
App.prototype.on = function(eventName, callback) {
  if (eventName === 'ready' && this.isReady()) {
    callback();
    return this;
  }
  return nativeOn.call(this, eventName, callback);
};

const nativeGetLocale = App.prototype.getLocale;
App.prototype.getLocale = function() {
  return nativeGetLocale.call(this) || 'en-US';
};
App.prototype.getSystemLocale = function() {
  return this.getLocale();
};
App.prototype.getPreferredSystemLanguages = function() {
  const locale = this.getLocale();
  return locale ? [locale] : ['en-US'];
};

app.commandLine = require('./api/command-line');

const nativeTheme = require('./api/native-theme');

Object.assign(electron, {
  app,
  clipboard: require('../common/api/clipboard'),
  dialog: require('./api/dialog').dialog,
  globalShortcut: require('./api/global-shortcut'),
  isPromise: require('../common/api/is-promise').isPromise,
  Menu: require('./api/menu'),
  MenuItem: require('./api/menu-item'),
  nativeImage: require('../common/api/native-image').NativeImage,
  nativeTheme,
  powerMonitor: require('./api/power-monitor'),
  powerSaveBlocker: require('./api/power-save-blocker'),
  protocol: require('./api/protocol').protocol,
  safeStorage: require('./api/safe-storage'),
  screen: require('./api/screen').Screen,
  shell: require('../common/api/shell').Shell,
  Tray: require('./api/tray').Tray
});

Object.assign(electronMain, electron);

module.exports = electron;
