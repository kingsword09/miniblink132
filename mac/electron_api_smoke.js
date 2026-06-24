const assert = require('assert');

const powerSaveBlocker = require('../electron/lib/browser/api/power-save-blocker');

assert.strictEqual(powerSaveBlocker.isStarted(123456), false);

const appId = powerSaveBlocker.start('prevent-app-suspension');
assert.strictEqual(Number.isInteger(appId), true);
assert.strictEqual(powerSaveBlocker.isStarted(appId), true);

const displayId = powerSaveBlocker.start('prevent-display-sleep');
assert.strictEqual(Number.isInteger(displayId), true);
assert.strictEqual(displayId, appId + 1);
assert.strictEqual(powerSaveBlocker.isStarted(displayId), true);

powerSaveBlocker.stop(appId);
assert.strictEqual(powerSaveBlocker.isStarted(appId), false);
assert.strictEqual(powerSaveBlocker.isStarted(displayId), true);

powerSaveBlocker.stop(appId);
assert.strictEqual(powerSaveBlocker.isStarted(appId), false);

assert.throws(
    function() { powerSaveBlocker.start('invalid-type'); },
    TypeError);

const nextId = powerSaveBlocker.start('prevent-app-suspension');
assert.strictEqual(nextId, displayId + 1);

powerSaveBlocker.stop(displayId);
powerSaveBlocker.stop(nextId);
assert.strictEqual(powerSaveBlocker.isStarted(displayId), false);
assert.strictEqual(powerSaveBlocker.isStarted(nextId), false);

console.log('PASS power-save-blocker-js-smoke');
