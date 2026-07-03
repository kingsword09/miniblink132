'use strict';

const electron = require('electron');
const electronRenderer = require('electron/renderer');

electron.clipboard = require('../common/api/clipboard');
electron.CallbacksRegistry = require('../common/api/callbacks-registry').CallbacksRegistry;
electron.contextBridge = require('./api/context-bridge');
electron.isPromise = require('../common/api/is-promise').isPromise;
electron.ipcRenderer = require('./api/ipc-renderer');
electron.nativeImage = require('../common/api/native-image').NativeImage;
electron.remote = require('./api/remote');
electron.screen = require('./api/screen');
electron.shell = require('../common/api/shell').Shell;
electron.webFrame = require('./api/web-frame');

module.exports = electron;
Object.assign(electronRenderer, electron);
