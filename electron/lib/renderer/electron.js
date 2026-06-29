'use strict';

const electron = require('electron');
const electronRenderer = require('electron/renderer');

electron.clipboard = require('../common/api/clipboard');
electron.contextBridge = require('./api/context-bridge');
electron.isPromise = require('../common/api/is-promise').isPromise;
electron.ipcRenderer = require('./api/ipc-renderer');
electron.nativeImage = require('../common/api/native-image').NativeImage;
electron.shell = require('../common/api/shell').Shell;

module.exports = electron;
Object.assign(electronRenderer, electron);
