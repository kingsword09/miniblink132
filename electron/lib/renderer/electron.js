'use strict';

const electron = require('electron');
const electronRenderer = require('electron/renderer');

electron.clipboard = require('../common/api/clipboard');
electron.isPromise = require('../common/api/is-promise').isPromise;
electron.nativeImage = require('../common/api/native-image').NativeImage;
electron.shell = require('../common/api/shell').Shell;

module.exports = electron;
Object.assign(electronRenderer, electron);
