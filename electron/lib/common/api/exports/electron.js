'use strict';

function defineLazyExport(target, name, getter) {
  Object.defineProperty(target, name, {
    enumerable: true,
    get: getter
  });
}

exports.defineProperties = function(exports) {
  defineLazyExport(exports, 'clipboard', function() {
    return require('../clipboard');
  });
  defineLazyExport(exports, 'nativeImage', function() {
    return require('../native-image').NativeImage;
  });
  defineLazyExport(exports, 'shell', function() {
    return require('../shell').Shell;
  });

  Object.defineProperty(exports, 'isPromise', {
    get: function() {
      return require('../is-promise').isPromise;
    }
  });

  return exports;
};
