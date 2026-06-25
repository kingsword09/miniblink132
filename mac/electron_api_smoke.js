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
            this._paths = Object.create(null);
            this._locale = 'zh-CN';
            this._singleInstanceLocked = false;
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

        setPath(name, path) {
            this._paths[name] = path;
        }

        getPath(name) {
            return this._paths[name] || '';
        }

        getLocale() {
            return this._locale;
        }

        quit() {
            this.emit('before-quit');
            this.emit('window-all-closed');
        }

        requestSingleInstanceLock() {
            if (this._singleInstanceLocked)
                return false;
            this._singleInstanceLocked = true;
            return true;
        }

        releaseSingleInstance() {
            this._singleInstanceLocked = false;
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

    app.setPath('userData', '/tmp/miniblink-user-data');
    app.setPath('userCache', '/tmp/miniblink-user-cache');
    assert.strictEqual(app.getPath('userData'), '/tmp/miniblink-user-data');
    assert.strictEqual(app.getPath('userCache'), '/tmp/miniblink-user-cache');
    assert.strictEqual(app.getLocale(), 'zh-CN');

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

    assert.strictEqual(typeof app.releaseSingleInstance, 'function');
    assert.strictEqual(app.requestSingleInstanceLock(), true);
    assert.strictEqual(app.requestSingleInstanceLock(), false);
    app.releaseSingleInstance();
    assert.strictEqual(app.requestSingleInstanceLock(), true);
    app.releaseSingleInstance();

    const lifecycleEvents = [];
    app.on('before-quit', function() { lifecycleEvents.push('before-quit'); });
    app.on('window-all-closed', function() { lifecycleEvents.push('window-all-closed'); });
    app.quit();
    assert.deepStrictEqual(lifecycleEvents, ['before-quit', 'window-all-closed']);

    const ready = app.whenReady();
    app.emit('ready');
    await ready;

    console.log('PASS app-js-smoke');
}

async function runElectronAppRuntimeSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const originalMbConsoleLog = global.mbConsoleLog;
    const Module = require('module');
    const originalLoad = Module._load;
    const electronShim = {};
    const electronMainShim = {};
    global.mbConsoleLog = function() {};

    class RuntimeFakeApp {
        constructor() {
            this._ready = false;
            this._paths = Object.create(null);
            this._locale = '';
            this._singleInstanceLocked = false;
        }

        isReady() {
            return this._ready;
        }

        _setIsReady() {
            this._ready = true;
        }

        setPath(name, path) {
            this._paths[name] = path;
        }

        getPath(name) {
            return this._paths[name] || '';
        }

        getLocale() {
            return this._locale;
        }

        quit() {
            this.emit('before-quit');
            this.emit('window-all-closed');
        }

        requestSingleInstanceLock() {
            if (this._singleInstanceLocked)
                return false;
            this._singleInstanceLocked = true;
            return true;
        }

        releaseSingleInstance() {
            this._singleInstanceLocked = false;
        }

        _setAppPath() {}
        _relaunch() {}
        makeSingleInstanceImpl() { return false; }
    }

    function BrowserWindow() {
        this.webContents = { once() {} };
    }
    BrowserWindow.prototype.show = function() {};
    function BrowserView() {}
    function WebContents() {}
    function Tray() {}
    function NativeImage() {}
    function MenuItem() {}

    const stubs = {
        './api/ipc-main': {},
        './../browser/api/browser-window.js': BrowserWindow,
        './../browser/api/browser-view.js': BrowserView,
        './../browser/api/web-contents.js': WebContents,
        './../browser/api/session.js': { session: {} },
        './../browser/api/command-line.js': {},
        './api/menu-item.js': MenuItem,
        './api/menu.js': { getApplicationMenu() { return null; } },
        './../common/api/is-promise.js': { isPromise(value) { return !!value && typeof value.then === 'function'; } },
        './api/dialog.js': { dialog: {} },
        './api/net.js': { net: {} },
        './api/utility-process.js': { utilityProcess: {} },
        './api/parent-port.js': {},
        './api/web-frame-main.js': { webFrameMain: {} },
        './../common/api/shell.js': { Shell: {} },
        './../common/api/screen.js': { Screen: {}, Tray },
        './../common/api/clipboard.js': {},
        './../common/api/native-image.js': { NativeImage },
        './api/safe-storage.js': {},
        './api/protocol.js': { protocol: {} },
        './api/tray': { Tray },
        './api/power-monitor': {},
        './api/power-save-blocker': {}
    };

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_app')
            return { App: RuntimeFakeApp };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    const electronPath = require.resolve('../electron/lib/browser/electron');
    const appPath = require.resolve('../electron/lib/browser/api/app');
    delete require.cache[electronPath];
    delete require.cache[appPath];

    Module._load = function(request, parent, isMain) {
        if (request === 'electron')
            return electronShim;
        if (request === 'electron/main')
            return electronMainShim;
        if (parent && parent.filename === electronPath && Object.prototype.hasOwnProperty.call(stubs, request))
            return stubs[request];
        return originalLoad.call(this, request, parent, isMain);
    };

    try {
        const electron = require('../electron/lib/browser/electron');
        const app = electron.app;

        assert.strictEqual(app.getLocale(), 'en-US');
        assert.deepStrictEqual(app.getPreferredSystemLanguages(), ['en-US']);
        app._locale = 'fr-FR';
        assert.strictEqual(app.getLocale(), 'fr-FR');
        assert.strictEqual(app.getSystemLocale(), 'fr-FR');
        assert.deepStrictEqual(app.getPreferredSystemLanguages(), ['fr-FR']);

        app.setPath('userData', '/tmp/runtime-user-data');
        assert.strictEqual(app.getPath('userData'), '/tmp/runtime-user-data');

        let readyCount = 0;
        app._setIsReady();
        app.on('ready', function() { readyCount++; });
        assert.strictEqual(readyCount, 1);
        await app.whenReady();

        const lifecycleEvents = [];
        app.on('before-quit', function() { lifecycleEvents.push('before-quit'); });
        app.on('window-all-closed', function() { lifecycleEvents.push('window-all-closed'); });
        app.quit();
        assert.deepStrictEqual(lifecycleEvents, ['before-quit', 'window-all-closed']);

        assert.strictEqual(app.requestSingleInstanceLock(), true);
        assert.strictEqual(app.requestSingleInstanceLock(), false);
        assert.strictEqual(typeof app.releaseSingleInstance, 'function');
        app.releaseSingleInstance();
        assert.strictEqual(app.requestSingleInstanceLock(), true);
        app.releaseSingleInstance();
    } finally {
        Module._load = originalLoad;
        process._linkedBinding = originalLinkedBinding;
        global.mbConsoleLog = originalMbConsoleLog;
        delete require.cache[electronPath];
        delete require.cache[appPath];
    }

    console.log('PASS app-runtime-js-smoke');
}

async function runShellApiSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const calls = [];

    const fakeShell = {
        showItemInFolder(path) {
            calls.push(['showItemInFolder', path]);
        },
        openPath(path) {
            calls.push(['openPath', path]);
            return true;
        },
        openExternal(url, options) {
            calls.push(['openExternal', url, options]);
            return true;
        },
        moveItemToTrash(path) {
            calls.push(['moveItemToTrash', path]);
            return path !== '/tmp/fail-trash';
        },
        beep() {
            calls.push(['beep']);
        }
    };

    process._linkedBinding = function(name) {
        if (name === 'electron_common_shell')
            return { Shell: fakeShell };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    delete require.cache[require.resolve('../electron/lib/common/api/shell')];
    const shellModule = require('../electron/lib/common/api/shell');
    process._linkedBinding = originalLinkedBinding;

    assert.strictEqual(shellModule.Shell, shellModule.shell);
    assert.strictEqual(shellModule.shell, fakeShell);
    assert.strictEqual(typeof fakeShell.openExternal, 'function');
    assert.strictEqual(typeof fakeShell.openPath, 'function');
    assert.strictEqual(typeof fakeShell.showItemInFolder, 'function');
    assert.strictEqual(typeof fakeShell.moveItemToTrash, 'function');
    assert.strictEqual(typeof fakeShell.trashItem, 'function');
    assert.strictEqual(typeof fakeShell.beep, 'function');

    fakeShell.openExternal('https://example.com/', { activate: false });
    fakeShell.openPath('/tmp/open-path.txt');
    fakeShell.showItemInFolder('/tmp/open-path.txt');
    fakeShell.beep();
    assert.strictEqual(fakeShell.moveItemToTrash('/tmp/legacy-trash.txt'), true);
    await fakeShell.trashItem('/tmp/trash-item.txt');
    await assert.rejects(fakeShell.trashItem('/tmp/fail-trash'), /Failed to move item to trash/);

    assert.deepStrictEqual(calls, [
        ['openExternal', 'https://example.com/', { activate: false }],
        ['openPath', '/tmp/open-path.txt'],
        ['showItemInFolder', '/tmp/open-path.txt'],
        ['beep'],
        ['moveItemToTrash', '/tmp/legacy-trash.txt'],
        ['moveItemToTrash', '/tmp/trash-item.txt'],
        ['moveItemToTrash', '/tmp/fail-trash']
    ]);

    console.log('PASS shell-js-smoke');
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
        return runElectronAppRuntimeSmoke();
    })
    .then(function() {
        return runShellApiSmoke();
    })
    .then(function() {
        runPowerSaveBlockerSmoke();
    })
    .catch(function(error) {
        console.error(error && error.stack ? error.stack : error);
        process.exit(1);
    });
