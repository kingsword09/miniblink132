'use strict';

const common = require('../../../common/api/exports/electron');

function defineLazyExport(target, name, getter) {
  Object.defineProperty(target, name, {
    enumerable: true,
    get: getter
  });
}

common.defineProperties(exports);

defineLazyExport(exports, 'app', function() {
  return require('../app').App;
});
defineLazyExport(exports, 'BrowserWindow', function() {
  return require('../browser-window');
});
defineLazyExport(exports, 'dialog', function() {
  return require('../dialog').dialog;
});
defineLazyExport(exports, 'globalShortcut', function() {
  return require('../global-shortcut');
});
defineLazyExport(exports, 'ipcMain', function() {
  return require('../ipc-main');
});
defineLazyExport(exports, 'Menu', function() {
  return require('../menu');
});
defineLazyExport(exports, 'MenuItem', function() {
  return require('../menu-item');
});
defineLazyExport(exports, 'MessageChannelMain', function() {
  return require('../message-channel-main').MessageChannelMain;
});
defineLazyExport(exports, 'nativeTheme', function() {
  return require('../native-theme');
});
defineLazyExport(exports, 'powerMonitor', function() {
  return require('../power-monitor');
});
defineLazyExport(exports, 'powerSaveBlocker', function() {
  return require('../power-save-blocker');
});
defineLazyExport(exports, 'protocol', function() {
  return require('../protocol').protocol;
});
defineLazyExport(exports, 'safeStorage', function() {
  return require('../safe-storage');
});
defineLazyExport(exports, 'screen', function() {
  return require('../screen').Screen;
});
defineLazyExport(exports, 'session', function() {
  return require('../session').session;
});
defineLazyExport(exports, 'Tray', function() {
  return require('../tray').Tray;
});
defineLazyExport(exports, 'webContents', function() {
  return require('../web-contents');
});
defineLazyExport(exports, 'webFrameMain', function() {
  return require('../web-frame-main').webFrameMain;
});
