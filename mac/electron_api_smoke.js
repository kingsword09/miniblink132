const assert = require('assert');

async function runAppApiSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const relaunchCalls = [];
    let singleInstanceCallback = null;

    class FakeApp {
        constructor() {
            this._ready = false;
            this._listeners = Object.create(null);
            this._name = '';
            this._version = '1.3.3';
            this._nativeAppPath = null;
        }

        isReady() {
            return this._ready;
        }

        on(name, callback) {
            this._listeners[name] = callback;
        }

        emit(name) {
            this._ready = this._ready || name === 'ready';
            if (this._listeners[name])
                this._listeners[name]();
        }

        setName(name) {
            this._name = name;
        }

        getName() {
            return this._name;
        }

        setVersion(version) {
            this._version = version;
        }

        getVersion() {
            return this._version;
        }

        _setAppPath(path) {
            this._nativeAppPath = path;
        }

        _relaunch(options) {
            relaunchCalls.push(JSON.parse(JSON.stringify(options)));
        }

        makeSingleInstanceImpl(callback) {
            singleInstanceCallback = callback;
            return true;
        }
    }

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_app')
            return { App: FakeApp };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    delete require.cache[require.resolve('../electron/lib/browser/api/app')];
    const App = require('../electron/lib/browser/api/app').App;
    process._linkedBinding = originalLinkedBinding;

    const app = new App();
    app.setName('MiniBlink App');
    app.setVersion('34.5.8');
    assert.strictEqual(app.getName(), 'MiniBlink App');
    assert.strictEqual(app.getVersion(), '34.5.8');

    app.setAppPath('/tmp/miniblink-app');
    assert.strictEqual(app.getAppPath(), '/tmp/miniblink-app');
    assert.strictEqual(app._nativeAppPath, '/tmp/miniblink-app');

    app.relaunch({ args: ['--ok'], execPath: '/Applications/MiniBlink.app' });
    app.relaunch({ args: '--bad', execPath: 42 });
    assert.deepStrictEqual(relaunchCalls[0], {
        args: ['--ok'],
        execPath: '/Applications/MiniBlink.app'
    });
    assert.deepStrictEqual(relaunchCalls[1], {
        args: [''],
        execPath: ''
    });

    let singleInstanceArgs = null;
    assert.strictEqual(app.makeSingleInstance(function(argv, cwd) {
        singleInstanceArgs = { argv, cwd };
    }), true);
    singleInstanceCallback(JSON.stringify(['only-argv']));
    assert.deepStrictEqual(singleInstanceArgs, {
        argv: ['only-argv'],
        cwd: ''
    });

    const ready = app.whenReady();
    app.emit('ready');
    await ready;

    console.log('PASS app-js-smoke');
}

const nativeStates = [];
global.__miniBlinkPowerSaveBlockerNative = {
    setExecutionState(state) {
        nativeStates.push(state);
    }
};

const ES_CONTINUOUS = 0x80000000;
const ES_SYSTEM_REQUIRED = 0x00000001;
const ES_DISPLAY_REQUIRED = 0x00000002;

function runPowerSaveBlockerSmoke() {
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
    assert.deepStrictEqual(nativeStates, [
        ES_CONTINUOUS + ES_SYSTEM_REQUIRED,
        ES_CONTINUOUS + ES_SYSTEM_REQUIRED + ES_DISPLAY_REQUIRED,
        ES_CONTINUOUS + ES_SYSTEM_REQUIRED,
        ES_CONTINUOUS
    ]);

    console.log('PASS power-save-blocker-js-smoke');
}

runAppApiSmoke()
    .then(function() {
        runPowerSaveBlockerSmoke();
    })
    .catch(function(error) {
        console.error(error && error.stack ? error.stack : error);
        process.exit(1);
    });
