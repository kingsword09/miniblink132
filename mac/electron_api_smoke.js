const assert = require('assert');

const rendererExcludedExports = ['webFrame', 'remote', 'ipcRenderer', 'contextBridge', 'screen', 'CallbacksRegistry'];

function assertRendererExportsExcluded(electron, label) {
    for (const key of rendererExcludedExports) {
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, key), false, `${label} ${key} should stay absent`);
    }
}

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
            this._quitting = false;
        }

        isReady() {
            return this._ready;
        }

        on(name, callback) {
            this._listeners[name] = callback;
        }

        emit(name) {
            const event = {
                defaultPrevented: false,
                preventDefault() {
                    this.defaultPrevented = true;
                },
                sender: this
            };
            this._ready = this._ready || name === 'ready';
            if (this._listeners[name])
                this._listeners[name](event);
            return event.defaultPrevented;
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
            if (this._quitting)
                return;
            this._quitting = true;
            if (this.emit('before-quit')) {
                this._quitting = false;
                return;
            }
            this.emit('window-all-closed');
            this.emit('quit');
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
    app.on('quit', function() { lifecycleEvents.push('quit'); });
    app.quit();
    assert.deepStrictEqual(lifecycleEvents, ['before-quit', 'window-all-closed', 'quit']);

    const cancelApp = new App();
    const cancelEvents = [];
    let cancelOnce = true;
    let cancelPrevented = false;
    cancelApp.on('before-quit', function(event) {
        cancelEvents.push('before-quit');
        if (cancelOnce) {
            cancelOnce = false;
            event.preventDefault();
            cancelPrevented = event.defaultPrevented === true;
        }
    });
    cancelApp.on('window-all-closed', function() { cancelEvents.push('window-all-closed'); });
    cancelApp.on('quit', function() { cancelEvents.push('quit'); });
    cancelApp.quit();
    assert.strictEqual(cancelPrevented, true);
    assert.deepStrictEqual(cancelEvents, ['before-quit']);

    const retryApp = new App();
    const retryEvents = [];
    retryApp.on('before-quit', function() { retryEvents.push('before-quit'); });
    retryApp.on('window-all-closed', function() { retryEvents.push('window-all-closed'); });
    retryApp.on('quit', function() { retryEvents.push('quit'); });
    retryApp.quit();
    assert.deepStrictEqual(retryEvents, ['before-quit', 'window-all-closed', 'quit']);

    const ready = app.whenReady();
    app.emit('ready');
    await ready;

    console.log('PASS app-js-smoke');
}

async function runElectronAppRuntimeSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const originalMbConsoleLog = global.mbConsoleLog;
    const hadNativeTheme = Object.prototype.hasOwnProperty.call(process.env, 'MINIBLINK_NATIVE_THEME');
    const originalNativeTheme = process.env.MINIBLINK_NATIVE_THEME;
    const hadSystemDarkMode = Object.prototype.hasOwnProperty.call(process.env, 'MINIBLINK_SYSTEM_DARK_MODE');
    const originalSystemDarkMode = process.env.MINIBLINK_SYSTEM_DARK_MODE;
    const hadNativeThemePollMs = Object.prototype.hasOwnProperty.call(process.env, 'MINIBLINK_NATIVE_THEME_POLL_MS');
    const originalNativeThemePollMs = process.env.MINIBLINK_NATIVE_THEME_POLL_MS;
    const hadHighContrast = Object.prototype.hasOwnProperty.call(process.env, 'MINIBLINK_HIGH_CONTRAST');
    const originalHighContrast = process.env.MINIBLINK_HIGH_CONTRAST;
    const Module = require('module');
    const originalLoad = Module._load;
    const electronShim = {};
    const electronMainShim = {};
    global.mbConsoleLog = function() {};

    class RuntimeFakeApp {
        constructor() {
            this._ready = false;
            this._listeners = Object.create(null);
            this._paths = Object.create(null);
            this._locale = '';
            this._singleInstanceLocked = false;
            this._quitting = false;
        }

        isReady() {
            return this._ready;
        }

        _setIsReady() {
            this._ready = true;
        }

        on(name, callback) {
            this._listeners[name] = callback;
        }

        emit(name) {
            const event = {
                defaultPrevented: false,
                preventDefault() {
                    this.defaultPrevented = true;
                },
                sender: this
            };
            this._ready = this._ready || name === 'ready';
            if (this._listeners[name])
                this._listeners[name](event);
            return event.defaultPrevented;
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
            if (this._quitting)
                return;
            this._quitting = true;
            if (this.emit('before-quit')) {
                this._quitting = false;
                return;
            }
            this.emit('window-all-closed');
            this.emit('quit');
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

    function Tray() {}
    function NativeImage() {}
    function MenuItem() {}

    const moduleMocks = {
        './api/command-line': {},
        './api/dialog': { dialog: {} },
        './api/global-shortcut': {},
        './api/menu': { getApplicationMenu() { return null; } },
        './api/menu-item': MenuItem,
        './api/power-monitor': {},
        './api/power-save-blocker': {},
        './api/protocol': { protocol: {} },
        './api/safe-storage': {},
        './api/screen': { Screen: {} },
        './api/tray': { Tray },
        '../common/api/clipboard': {},
        '../common/api/is-promise': { isPromise(value) { return !!value && typeof value.then === 'function'; } },
        '../common/api/native-image': { NativeImage },
        '../common/api/shell': { Shell: {} }
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
    const nativeThemePath = require.resolve('../electron/lib/browser/api/native-theme');
    const browserExportsPath = require.resolve('../electron/lib/browser/api/exports/electron');
    delete require.cache[electronPath];
    delete require.cache[appPath];
    delete require.cache[nativeThemePath];
    delete require.cache[browserExportsPath];

    Module._load = function(request, parent, isMain) {
        if (request === 'electron')
            return electronShim;
        if (request === 'electron/main')
            return electronMainShim;
        if (parent && parent.filename === electronPath && Object.prototype.hasOwnProperty.call(moduleMocks, request))
            return moduleMocks[request];
        return originalLoad.call(this, request, parent, isMain);
    };

    try {
        delete process.env.MINIBLINK_NATIVE_THEME;
        process.env.MINIBLINK_SYSTEM_DARK_MODE = '0';
        process.env.MINIBLINK_NATIVE_THEME_POLL_MS = '10';
        delete process.env.MINIBLINK_HIGH_CONTRAST;

        const electron = require('../electron/lib/browser/electron');
        const browserExports = require('../electron/lib/browser/api/exports/electron');
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
        app.on('quit', function() { lifecycleEvents.push('quit'); });
        app.quit();
        assert.deepStrictEqual(lifecycleEvents, ['before-quit', 'window-all-closed', 'quit']);

        const cancelApp = new RuntimeFakeApp();
        const cancelEvents = [];
        let cancelOnce = true;
        cancelApp.on('before-quit', function(event) {
            cancelEvents.push('before-quit');
            if (cancelOnce) {
                cancelOnce = false;
                event.preventDefault();
            }
        });
        cancelApp.on('window-all-closed', function() { cancelEvents.push('window-all-closed'); });
        cancelApp.on('quit', function() { cancelEvents.push('quit'); });
        cancelApp.quit();
        assert.deepStrictEqual(cancelEvents, ['before-quit']);

        assert.strictEqual(app.requestSingleInstanceLock(), true);
        assert.strictEqual(app.requestSingleInstanceLock(), false);
        assert.strictEqual(typeof app.releaseSingleInstance, 'function');
        app.releaseSingleInstance();
        assert.strictEqual(app.requestSingleInstanceLock(), true);
        app.releaseSingleInstance();

        assert.strictEqual(electron.nativeTheme.themeSource, 'system');
        assert.strictEqual(browserExports.app, RuntimeFakeApp);
        assert.strictEqual(browserExports.nativeTheme, electron.nativeTheme);
        assert.strictEqual(electronMainShim.nativeTheme, electron.nativeTheme);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'BrowserWindow'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'BrowserView'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'contentTracing'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'crashReporter'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'ipcMain'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'MessageChannelMain'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'net'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'session'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'systemPreferences'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'TouchBar'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'utilityProcess'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'webContents'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(electron, 'webFrameMain'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(browserExports, 'BrowserWindow'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(browserExports, 'session'), false);
        assert.strictEqual(Object.prototype.hasOwnProperty.call(browserExports, 'webContents'), false);
        assert.strictEqual(electron.nativeTheme.shouldUseDarkColors, false);
        assert.strictEqual(electron.nativeTheme.shouldUseHighContrastColors, false);
        assert.strictEqual(electron.nativeTheme.shouldUseInvertedColorScheme, false);
        assert.strictEqual(electron.nativeTheme.on('updated', function() {}), electron.nativeTheme);
        electron.nativeTheme.removeAllListeners('updated');

        let updatedCount = 0;
        electron.nativeTheme.on('updated', function() { updatedCount++; });
        electron.nativeTheme.themeSource = 'dark';
        assert.strictEqual(electron.nativeTheme.themeSource, 'dark');
        assert.strictEqual(electron.nativeTheme.shouldUseDarkColors, true);
        assert.strictEqual(updatedCount, 1);

        process.env.MINIBLINK_HIGH_CONTRAST = '1';
        assert.strictEqual(electron.nativeTheme.shouldUseHighContrastColors, true);

        electron.nativeTheme.themeSource = 'light';
        assert.strictEqual(electron.nativeTheme.shouldUseDarkColors, false);
        assert.strictEqual(updatedCount, 2);

        electron.nativeTheme.themeSource = '';
        assert.strictEqual(electron.nativeTheme.themeSource, 'system');
        assert.strictEqual(updatedCount, 3);
        assert.strictEqual(electron.nativeTheme.shouldUseDarkColors, false);

        process.env.MINIBLINK_SYSTEM_DARK_MODE = '1';
        await new Promise(function(resolve) { setTimeout(resolve, 30); });
        assert.strictEqual(electron.nativeTheme.shouldUseDarkColors, true);
        assert.ok(updatedCount >= 4);

        const countBeforeRemove = updatedCount;
        electron.nativeTheme.removeAllListeners('updated');
        assert.strictEqual(electron.nativeTheme.listenerCount('updated'), 0);
        process.env.MINIBLINK_SYSTEM_DARK_MODE = '0';
        await new Promise(function(resolve) { setTimeout(resolve, 30); });
        assert.strictEqual(updatedCount, countBeforeRemove);
        console.log('PASS native-theme-js-smoke');
    } finally {
        Module._load = originalLoad;
        process._linkedBinding = originalLinkedBinding;
        global.mbConsoleLog = originalMbConsoleLog;
        if (hadNativeTheme)
            process.env.MINIBLINK_NATIVE_THEME = originalNativeTheme;
        else
            delete process.env.MINIBLINK_NATIVE_THEME;
        if (hadSystemDarkMode)
            process.env.MINIBLINK_SYSTEM_DARK_MODE = originalSystemDarkMode;
        else
            delete process.env.MINIBLINK_SYSTEM_DARK_MODE;
        if (hadNativeThemePollMs)
            process.env.MINIBLINK_NATIVE_THEME_POLL_MS = originalNativeThemePollMs;
        else
            delete process.env.MINIBLINK_NATIVE_THEME_POLL_MS;
        if (hadHighContrast)
            process.env.MINIBLINK_HIGH_CONTRAST = originalHighContrast;
        else
            delete process.env.MINIBLINK_HIGH_CONTRAST;
        delete require.cache[electronPath];
        delete require.cache[appPath];
        delete require.cache[nativeThemePath];
        delete require.cache[browserExportsPath];
    }

    console.log('PASS app-runtime-js-smoke');
}

async function runNativeThemeNativeNotificationSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const originalSetInterval = global.setInterval;
    const hadNativeTheme = Object.prototype.hasOwnProperty.call(process.env, 'MINIBLINK_NATIVE_THEME');
    const originalNativeTheme = process.env.MINIBLINK_NATIVE_THEME;
    const hadSystemDarkMode = Object.prototype.hasOwnProperty.call(process.env, 'MINIBLINK_SYSTEM_DARK_MODE');
    const originalSystemDarkMode = process.env.MINIBLINK_SYSTEM_DARK_MODE;
    const nativeThemePath = require.resolve('../electron/lib/browser/api/native-theme');
    const cachedModule = require.cache[nativeThemePath];
    let systemDark = false;
    let callback = null;
    let startCount = 0;
    let stopCount = 0;

    delete require.cache[nativeThemePath];
    delete process.env.MINIBLINK_NATIVE_THEME;
    delete process.env.MINIBLINK_SYSTEM_DARK_MODE;

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_native_theme') {
            return {
                shouldUseDarkColors() {
                    return systemDark;
                },
                startWatching(listener) {
                    startCount++;
                    callback = listener;
                    return listener;
                },
                stopWatching(listener) {
                    assert.strictEqual(listener, callback);
                    stopCount++;
                    callback = null;
                }
            };
        }
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    global.setInterval = function() {
        throw new Error('nativeTheme should use native notification binding instead of polling');
    };

    try {
        const nativeTheme = require('../electron/lib/browser/api/native-theme');
        let updatedCount = 0;

        assert.strictEqual(nativeTheme.themeSource, 'system');
        assert.strictEqual(nativeTheme.shouldUseDarkColors, false);
        nativeTheme.on('updated', function() { updatedCount++; });
        assert.strictEqual(startCount, 1);
        assert.strictEqual(typeof callback, 'function');

        systemDark = true;
        callback();
        assert.strictEqual(nativeTheme.shouldUseDarkColors, true);
        assert.strictEqual(updatedCount, 1);

        callback();
        assert.strictEqual(updatedCount, 1);

        nativeTheme.themeSource = 'light';
        assert.strictEqual(updatedCount, 2);
        systemDark = false;
        callback();
        assert.strictEqual(updatedCount, 2);

        nativeTheme.themeSource = 'system';
        assert.strictEqual(updatedCount, 3);
        systemDark = true;
        callback();
        assert.strictEqual(updatedCount, 4);

        nativeTheme.removeAllListeners('updated');
        assert.strictEqual(stopCount, 1);
        assert.strictEqual(callback, null);
        console.log('PASS native-theme-native-notification-smoke');
    } finally {
        global.setInterval = originalSetInterval;
        process._linkedBinding = originalLinkedBinding;
        if (hadNativeTheme)
            process.env.MINIBLINK_NATIVE_THEME = originalNativeTheme;
        else
            delete process.env.MINIBLINK_NATIVE_THEME;
        if (hadSystemDarkMode)
            process.env.MINIBLINK_SYSTEM_DARK_MODE = originalSystemDarkMode;
        else
            delete process.env.MINIBLINK_SYSTEM_DARK_MODE;
        if (cachedModule)
            require.cache[nativeThemePath] = cachedModule;
        else
            delete require.cache[nativeThemePath];
    }
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
            if (path === '/tmp/fail-open')
                return false;
            if (path === '/tmp/native-error')
                return 'Native open failed';
            if (path === '/tmp/throw-open')
                throw new Error('Open exception');
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
    assert.strictEqual(await fakeShell.openPath('/tmp/open-path.txt'), '');
    assert.strictEqual(await fakeShell.openPath('/tmp/fail-open'), 'Failed to open path');
    assert.strictEqual(await fakeShell.openPath('/tmp/native-error'), 'Native open failed');
    assert.strictEqual(await fakeShell.openPath('/tmp/throw-open'), 'Open exception');
    fakeShell.showItemInFolder('/tmp/open-path.txt');
    fakeShell.beep();
    assert.strictEqual(fakeShell.moveItemToTrash('/tmp/legacy-trash.txt'), true);
    await fakeShell.trashItem('/tmp/trash-item.txt');
    await assert.rejects(fakeShell.trashItem('/tmp/fail-trash'), /Failed to move item to trash/);

    assert.deepStrictEqual(calls, [
        ['openExternal', 'https://example.com/', { activate: false }],
        ['openPath', '/tmp/open-path.txt'],
        ['openPath', '/tmp/fail-open'],
        ['openPath', '/tmp/native-error'],
        ['openPath', '/tmp/throw-open'],
        ['showItemInFolder', '/tmp/open-path.txt'],
        ['beep'],
        ['moveItemToTrash', '/tmp/legacy-trash.txt'],
        ['moveItemToTrash', '/tmp/trash-item.txt'],
        ['moveItemToTrash', '/tmp/fail-trash']
    ]);

    console.log('PASS shell-js-smoke');
}

function runScreenApiSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const modulePath = require.resolve('../electron/lib/common/api/screen');
    const cachedModule = require.cache[modulePath];
    const calls = [];

    const primaryDisplay = {
        id: 1,
        rotation: 0,
        scaleFactor: 1,
        touchSupport: 'unavailable',
        bounds: { x: 0, y: 0, width: 1920, height: 1080 },
        size: { width: 1920, height: 1080 },
        workArea: { x: 0, y: 23, width: 1920, height: 1057 },
        workAreaSize: { width: 1920, height: 1057 }
    };

    class FakeScreen {
        getCursorScreenPoint() {
            calls.push(['getCursorScreenPoint']);
            return { x: 10, y: 20 };
        }

        getPrimaryDisplay() {
            calls.push(['getPrimaryDisplay']);
            return primaryDisplay;
        }

        getAllDisplays() {
            calls.push(['getAllDisplays']);
            return [primaryDisplay];
        }

        getDisplayNearestPoint(point) {
            calls.push(['getDisplayNearestPoint', point]);
            return primaryDisplay;
        }

        getDisplayMatching(rect) {
            calls.push(['getDisplayMatching', rect]);
            return primaryDisplay;
        }
    }

    process._linkedBinding = function(name) {
        if (name === 'electron_common_screen')
            return { Screen: FakeScreen, Tray: function Tray() {} };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    let screenModule;
    try {
        delete require.cache[modulePath];
        screenModule = require(modulePath);
    } finally {
        process._linkedBinding = originalLinkedBinding;
        if (cachedModule)
            require.cache[modulePath] = cachedModule;
        else
            delete require.cache[modulePath];
    }

    assert.strictEqual(screenModule.Screen, screenModule.screen);
    assert.strictEqual(typeof screenModule.screen.on, 'function');
    assert.strictEqual(typeof screenModule.screen.getCursorScreenPoint, 'function');
    assert.strictEqual(typeof screenModule.screen.getPrimaryDisplay, 'function');
    assert.strictEqual(typeof screenModule.screen.getAllDisplays, 'function');
    assert.strictEqual(typeof screenModule.screen.getDisplayNearestPoint, 'function');
    assert.strictEqual(typeof screenModule.screen.getDisplayMatching, 'function');

    assert.deepStrictEqual(screenModule.screen.getCursorScreenPoint(), { x: 10, y: 20 });
    assert.strictEqual(screenModule.screen.getPrimaryDisplay(), primaryDisplay);
    assert.deepStrictEqual(screenModule.screen.getAllDisplays(), [primaryDisplay]);
    assert.strictEqual(screenModule.screen.getDisplayNearestPoint({ x: 10, y: 20 }), primaryDisplay);
    assert.strictEqual(screenModule.screen.getDisplayMatching({ x: 0, y: 0, width: 1, height: 1 }), primaryDisplay);
    assert.deepStrictEqual(calls, [
        ['getCursorScreenPoint'],
        ['getPrimaryDisplay'],
        ['getAllDisplays'],
        ['getDisplayNearestPoint', { x: 10, y: 20 }],
        ['getDisplayMatching', { x: 0, y: 0, width: 1, height: 1 }]
    ]);

    console.log('PASS screen-js-smoke');
}

function runGlobalShortcutSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const modulePath = require.resolve('../electron/lib/browser/api/global-shortcut');
    const cachedModule = require.cache[modulePath];
    delete global.__miniBlinkGlobalShortcutNative;

    try {
        process._linkedBinding = function(name) {
            if (originalLinkedBinding)
                return originalLinkedBinding.call(process, name);
            throw new Error('unexpected linked binding: ' + name);
        };

        delete require.cache[modulePath];
        const globalShortcut = require('../electron/lib/browser/api/global-shortcut');
        const primary = process.platform === 'darwin' ? 'Command+Shift+G' : 'Control+Shift+G';

        assert.strictEqual(globalShortcut.isRegistered('CommandOrControl+Shift+G'), false);
        assert.strictEqual(globalShortcut.register('', function() {}), false);
        assert.strictEqual(globalShortcut.register('CommandOrControl+Shift+G'), false);
        assert.strictEqual(globalShortcut.register('CommandOrControl+Shift+G', function() {}), true);
        assert.strictEqual(globalShortcut.isRegistered(primary), true);
        assert.strictEqual(globalShortcut.register(primary, function() {}), false);
        globalShortcut.unregister(primary);
        assert.strictEqual(globalShortcut.isRegistered('CommandOrControl+Shift+G'), false);
        globalShortcut.unregisterAll();

        const nativeCalls = [];
        const bindingCalls = [];
        let nativeCallback = null;
        const nativeBinding = {
            register(accelerator, callback) {
                nativeCalls.push(['register', accelerator]);
                if (accelerator === 'Alt+Blocked')
                    return false;
                nativeCallback = callback;
                return true;
            },
            unregister(accelerator) {
                nativeCalls.push(['unregister', accelerator]);
            },
            unregisterAll() {
                nativeCalls.push(['unregisterAll']);
            }
        };

        process._linkedBinding = function(name) {
            bindingCalls.push(name);
            if (name === 'electron_browser_global_shortcut')
                return nativeBinding;
            if (originalLinkedBinding)
                return originalLinkedBinding.call(process, name);
            throw new Error('unexpected linked binding: ' + name);
        };

        let callbackCount = 0;
        assert.strictEqual(globalShortcut.register('Alt+X', function() { callbackCount++; }), true);
        assert.strictEqual(globalShortcut.register('Alt+Blocked', function() {}), false);
        assert.strictEqual(globalShortcut.isRegistered('Option+X'), true);
        nativeCallback();
        assert.strictEqual(callbackCount, 1);
        globalShortcut.unregister('Option+X');
        assert.strictEqual(globalShortcut.isRegistered('Alt+X'), false);
        assert.strictEqual(globalShortcut.register('Shift+F5', function() {}), true);
        assert.strictEqual(globalShortcut.register('Super+Space', function() {}), true);
        globalShortcut.unregisterAll();
        assert.strictEqual(globalShortcut.isRegistered('Shift+F5'), false);
        assert.strictEqual(globalShortcut.isRegistered('Super+Space'), false);
        assert.deepStrictEqual(nativeCalls, [
            ['register', 'Alt+X'],
            ['register', 'Alt+Blocked'],
            ['unregister', 'Alt+X'],
            ['register', 'Shift+F5'],
            ['register', 'Super+Space'],
            ['unregisterAll']
        ]);
        assert.deepStrictEqual(bindingCalls, [
            'electron_browser_global_shortcut',
            'electron_browser_global_shortcut',
            'electron_browser_global_shortcut',
            'electron_browser_global_shortcut',
            'electron_browser_global_shortcut',
            'electron_browser_global_shortcut'
        ]);

        nativeCalls.length = 0;
        bindingCalls.length = 0;
        nativeCallback = null;
        global.__miniBlinkGlobalShortcutNative = nativeBinding;
        assert.strictEqual(globalShortcut.register('Control+Y', function() {}), true);
        globalShortcut.unregister('Control+Y');
        assert.deepStrictEqual(bindingCalls, []);
        assert.deepStrictEqual(nativeCalls, [
            ['register', 'Control+Y'],
            ['unregister', 'Control+Y']
        ]);
    } finally {
        process._linkedBinding = originalLinkedBinding;
        delete global.__miniBlinkGlobalShortcutNative;
        if (cachedModule)
            require.cache[modulePath] = cachedModule;
        else
            delete require.cache[modulePath];
    }

    console.log('PASS global-shortcut-js-smoke');
}

function runPowerMonitorSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const modulePath = require.resolve('../electron/lib/browser/api/power-monitor');
    const cachedModule = require.cache[modulePath];
    const calls = [];

    class FakePowerMonitor {
        getSystemIdleState(idleThreshold) {
            calls.push(['getSystemIdleState', idleThreshold]);
            if (idleThreshold <= 0)
                throw new TypeError('Invalid idle threshold, must be greater than 0');
            return idleThreshold >= 60 ? 'idle' : 'active';
        }

        getSystemIdleTime() {
            calls.push(['getSystemIdleTime']);
            return 7;
        }

        isOnBatteryPower() {
            calls.push(['isOnBatteryPower']);
            return true;
        }
    }

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_powermonitor')
            return { ApiPowerMonitor: FakePowerMonitor };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    let powerMonitor;
    try {
        delete require.cache[modulePath];
        powerMonitor = require(modulePath);
    } finally {
        process._linkedBinding = originalLinkedBinding;
        if (cachedModule)
            require.cache[modulePath] = cachedModule;
        else
            delete require.cache[modulePath];
    }

    assert.strictEqual(typeof powerMonitor.on, 'function');
    assert.strictEqual(typeof powerMonitor.emit, 'function');
    assert.strictEqual(typeof powerMonitor.getSystemIdleState, 'function');
    assert.strictEqual(typeof powerMonitor.getSystemIdleTime, 'function');
    assert.strictEqual(typeof powerMonitor.isOnBatteryPower, 'function');

    const events = [];
    ['suspend', 'resume', 'on-ac', 'on-battery', 'shutdown'].forEach(function(name) {
        assert.strictEqual(powerMonitor.on(name, function() { events.push(name); }), powerMonitor);
    });
    powerMonitor.emit('suspend');
    powerMonitor.emit('resume');
    powerMonitor.emit('on-ac');
    powerMonitor.emit('on-battery');
    powerMonitor.emit('shutdown');
    assert.deepStrictEqual(events, ['suspend', 'resume', 'on-ac', 'on-battery', 'shutdown']);

    assert.strictEqual(powerMonitor.getSystemIdleState(1), 'active');
    assert.strictEqual(powerMonitor.getSystemIdleState(60), 'idle');
    assert.throws(function() { powerMonitor.getSystemIdleState(0); }, TypeError);
    assert.strictEqual(powerMonitor.getSystemIdleTime(), 7);
    assert.strictEqual(powerMonitor.isOnBatteryPower(), true);
    assert.deepStrictEqual(calls, [
        ['getSystemIdleState', 1],
        ['getSystemIdleState', 60],
        ['getSystemIdleState', 0],
        ['getSystemIdleTime'],
        ['isOnBatteryPower']
    ]);
    powerMonitor.removeAllListeners();

    console.log('PASS power-monitor-js-smoke');
}

const ES_CONTINUOUS = 0x80000000;
const ES_SYSTEM_REQUIRED = 0x00000001;
const ES_DISPLAY_REQUIRED = 0x00000002;

function runPowerSaveBlockerSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const modulePath = require.resolve('../electron/lib/browser/api/power-save-blocker');
    const cachedModule = require.cache[modulePath];
    const nativeStates = [];
    const bindingCalls = [];

    process._linkedBinding = function(name) {
        bindingCalls.push(name);
        if (name === 'electron_browser_power_save_blocker') {
            return {
                setExecutionState(state) {
                    nativeStates.push(state);
                }
            };
        }
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    let powerSaveBlocker;
    try {
        delete require.cache[modulePath];
        powerSaveBlocker = require(modulePath);
    } finally {
        process._linkedBinding = originalLinkedBinding;
        if (cachedModule)
            require.cache[modulePath] = cachedModule;
        else
            delete require.cache[modulePath];
    }

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
    assert.deepStrictEqual(bindingCalls, ['electron_browser_power_save_blocker']);

    console.log('PASS power-save-blocker-js-smoke');
}


function runElectronRendererModuleExportsSmoke() {
    const Module = require('module');
    const originalLoad = Module._load;
    const electronMock = {};
    const electronRendererMock = {};
    const NativeImage = function NativeImage() {};
    const moduleMocks = {
        '../common/api/clipboard': {},
        '../common/api/is-promise': { isPromise(value) { return !!value && typeof value.then === 'function'; } },
        '../common/api/native-image': { NativeImage },
        '../common/api/shell': { Shell: {} }
    };
    const modulePath = require.resolve('../electron/lib/renderer/electron');

    delete require.cache[modulePath];
    Module._load=function(request,parent,isMain){
        if(request==='electron') return electronMock;
        if(request==='electron/renderer') return electronRendererMock;
        if(parent && parent.filename === modulePath && Object.prototype.hasOwnProperty.call(moduleMocks, request))
            return moduleMocks[request];
        return originalLoad.call(this,request,parent,isMain);
    }
    try {
        const electron = require('../electron/lib/renderer/electron');
        assert.strictEqual(electron, electronMock);
        assertRendererExportsExcluded(electron, 'renderer module export');
        assert.strictEqual(typeof electron.clipboard, 'object');
        assert.strictEqual(typeof electron.isPromise, 'function');
        assert.strictEqual(typeof electron.nativeImage, 'function');
        assert.strictEqual(typeof electron.shell, 'object');
        console.log('PASS renderer-module-exports-smoke');
    } finally {
        Module._load=originalLoad;
        delete require.cache[modulePath];
    }
}
function runRendererExportSurfaceSmoke() {
    const Module = require('module');
    const originalLoad = Module._load;
    const electronShim = {};
    const electronRendererShim = {};
    const NativeImage = function NativeImage() {};
    const moduleMocks = {
        '../common/api/clipboard': {},
        '../common/api/is-promise': { isPromise(value) { return !!value && typeof value.then === 'function'; } },
        '../common/api/native-image': { NativeImage },
        '../common/api/shell': { Shell: {} }
    };
    const rendererPath = require.resolve('../electron/lib/renderer/electron');

    delete require.cache[rendererPath];
    Module._load = function(request, parent, isMain) {
        if (request === 'electron')
            return electronShim;
        if (request === 'electron/renderer')
            return electronRendererShim;
        if (parent && parent.filename === rendererPath && Object.prototype.hasOwnProperty.call(moduleMocks, request))
            return moduleMocks[request];
        return originalLoad.call(this, request, parent, isMain);
    };

    try {
        const electron = require('../electron/lib/renderer/electron');
        assert.strictEqual(electron, electronShim);
        assert.strictEqual(electronRendererShim.clipboard, electron.clipboard);
        assert.strictEqual(electronRendererShim.nativeImage, NativeImage);
        assert.strictEqual(electronRendererShim.shell, electron.shell);
        assert.strictEqual(electron.isPromise(Promise.resolve()), true);
        assert.strictEqual(electron.isPromise({}), false);
        assertRendererExportsExcluded(electron, 'renderer export');
        console.log('PASS renderer-export-surface-smoke');
    } finally {
        Module._load = originalLoad;
        delete require.cache[rendererPath];
    }
}

runAppApiSmoke()
    .then(function() {
        return runElectronAppRuntimeSmoke();
    })
    .then(function() {
        return runNativeThemeNativeNotificationSmoke();
    })
    .then(function() {
        return runShellApiSmoke();
    })
    .then(function() {
        runScreenApiSmoke();
    })
    .then(function() {
        runGlobalShortcutSmoke();
    })
    .then(function() {
        runPowerMonitorSmoke();
    })
    .then(function() {
        runPowerSaveBlockerSmoke();
    })
    .then(function() {
        runRendererExportSurfaceSmoke();
    })
    .then(function() {
        runElectronRendererModuleExportsSmoke();
    })
    .catch(function(error) {
        console.error(error && error.stack ? error.stack : error);
        process.exit(1);
    });
