const assert = require('assert');

const rendererExcludedExports = [];

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

    class RuntimeFakeWebContents {
        static fromId() { return null; }
        static getAllWebContents() { return []; }
        static getFocusedWebContents() { return null; }
    }

    class RuntimeFakeBrowserWindow {
        static fromId() { return null; }
        static fromWebContents() { return null; }
        static getAllWindows() { return []; }
        static getFocusedWindow() { return null; }
    }

    class RuntimeFakeSession {
        constructor(partition) {
            this.partition = partition || '';
        }

        static fromPartition(partition) {
            return new RuntimeFakeSession(partition);
        }
    }

    class RuntimeFakeWebRequest {}
    class RuntimeFakeDownloadItem {}
    class RuntimeFakeBrowserView {}
    function RuntimeFakeMessageChannelMain() {}
    const RuntimeFakeWebFrameMain = {
        fromId() { return null; },
        fromIdOrNull() { return null; }
    };
    const RuntimeFakeNet = {
        request() {},
        isOnline() { return true; }
    };
    const RuntimeFakeUtilityProcess = {
        fork() {}
    };
    const RuntimeFakeSystemPreferences = {
        getAccentColor() { return '112233'; }
    };
    const RuntimeFakeContentTracing = {
        startRecording() { return Promise.resolve(); },
        stopRecording() { return Promise.resolve('/tmp/trace.json'); },
        getCategories() { return Promise.resolve([]); },
        getTraceBufferUsage() { return Promise.resolve({ value: 0, percentage: 0 }); }
    };
    const RuntimeFakeCrashReporter = {
        start() {},
        getParameters() { return {}; },
        addExtraParameter() {},
        removeExtraParameter() {}
    };
    function RuntimeFakeTouchBar() {}

    function Tray() {}
    function NativeImage() {}
    function MenuItem() {}

    const moduleMocks = {
        './api/command-line': {},
        './api/dialog': { dialog: {} },
        './api/global-shortcut': {},
        './api/browser-view': RuntimeFakeBrowserView,
        '../browser-view': RuntimeFakeBrowserView,
        './api/content-tracing': RuntimeFakeContentTracing,
        '../content-tracing': RuntimeFakeContentTracing,
        './api/crash-reporter': RuntimeFakeCrashReporter,
        '../crash-reporter': RuntimeFakeCrashReporter,
        './api/menu': { getApplicationMenu() { return null; } },
        './api/menu-item': MenuItem,
        './api/message-channel-main': { MessageChannelMain: RuntimeFakeMessageChannelMain },
        '../message-channel-main': { MessageChannelMain: RuntimeFakeMessageChannelMain },
        './api/web-frame-main': { webFrameMain: RuntimeFakeWebFrameMain },
        '../web-frame-main': { webFrameMain: RuntimeFakeWebFrameMain },
        './api/net': { net: RuntimeFakeNet },
        '../net': { net: RuntimeFakeNet },
        './api/utility-process': { utilityProcess: RuntimeFakeUtilityProcess },
        '../utility-process': { utilityProcess: RuntimeFakeUtilityProcess },
        './api/power-monitor': {},
        './api/power-save-blocker': {},
        './api/protocol': { protocol: {} },
        './api/safe-storage': {},
        './api/screen': { Screen: {} },
        './api/system-preferences': RuntimeFakeSystemPreferences,
        '../system-preferences': RuntimeFakeSystemPreferences,
        './api/touch-bar': RuntimeFakeTouchBar,
        '../touch-bar': RuntimeFakeTouchBar,
        './api/tray': { Tray },
        '../common/api/clipboard': {},
        '../common/api/is-promise': { isPromise(value) { return !!value && typeof value.then === 'function'; } },
        '../common/api/native-image': { NativeImage },
        '../common/api/shell': { Shell: {} }
    };

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_app')
            return { App: RuntimeFakeApp };
        if (name === 'electron_browser_web_contents')
            return { WebContents: RuntimeFakeWebContents };
        if (name === 'electron_browser_browserwindow')
            return { BrowserWindow: RuntimeFakeBrowserWindow };
        if (name === 'electron_browser_browserview')
            return { BrowserView: RuntimeFakeBrowserView };
        if (name === 'electron_browser_session')
            return { Session: RuntimeFakeSession };
        if (name === 'electron_browser_webrequest')
            return { WebRequest: RuntimeFakeWebRequest };
        if (name === 'electron_browser_downloaditem')
            return { DownloadItem: RuntimeFakeDownloadItem };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    const electronPath = require.resolve('../electron/lib/browser/electron');
    const appPath = require.resolve('../electron/lib/browser/api/app');
    const nativeThemePath = require.resolve('../electron/lib/browser/api/native-theme');
    const browserExportsPath = require.resolve('../electron/lib/browser/api/exports/electron');
    const browserWindowPath = require.resolve('../electron/lib/browser/api/browser-window');
    const webContentsPath = require.resolve('../electron/lib/browser/api/web-contents');
    const ipcMainPath = require.resolve('../electron/lib/browser/api/ipc-main');
    const sessionPath = require.resolve('../electron/lib/browser/api/session');
    const webFrameMainPath = require.resolve('../electron/lib/browser/api/web-frame-main');
    delete require.cache[electronPath];
    delete require.cache[appPath];
    delete require.cache[nativeThemePath];
    delete require.cache[browserExportsPath];
    delete require.cache[browserWindowPath];
    delete require.cache[webContentsPath];
    delete require.cache[ipcMainPath];
    delete require.cache[sessionPath];
    delete require.cache[webFrameMainPath];

    Module._load = function(request, parent, isMain) {
        if (request === 'electron')
            return electronShim;
        if (request === 'electron/main')
            return electronMainShim;
        if (parent && (parent.filename === electronPath || parent.filename === browserExportsPath) && Object.prototype.hasOwnProperty.call(moduleMocks, request))
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
        assert.strictEqual(electron.BrowserView, RuntimeFakeBrowserView);
        assert.strictEqual(electronMainShim.BrowserView, RuntimeFakeBrowserView);
        assert.strictEqual(browserExports.BrowserView, RuntimeFakeBrowserView);
        assert.strictEqual(electron.BrowserWindow, RuntimeFakeBrowserWindow);
        assert.strictEqual(electronMainShim.BrowserWindow, RuntimeFakeBrowserWindow);
        assert.strictEqual(browserExports.BrowserWindow, RuntimeFakeBrowserWindow);
        assert.strictEqual(electron.webContents, RuntimeFakeWebContents);
        assert.strictEqual(electronMainShim.webContents, RuntimeFakeWebContents);
        assert.strictEqual(browserExports.webContents, RuntimeFakeWebContents);
        assert.strictEqual(electron.session, RuntimeFakeSession);
        assert.strictEqual(electronMainShim.session, RuntimeFakeSession);
        assert.strictEqual(browserExports.session, RuntimeFakeSession);
        assert.ok(RuntimeFakeSession.defaultSession instanceof RuntimeFakeSession);
        assert.strictEqual(RuntimeFakeSession.defaultSession.partition, '');
        assert.strictEqual(typeof electron.ipcMain.on, 'function');
        assert.strictEqual(typeof electron.ipcMain.handle, 'function');
        assert.strictEqual(electronMainShim.ipcMain, electron.ipcMain);
        assert.strictEqual(browserExports.ipcMain, electron.ipcMain);
        assert.strictEqual(electron.MessageChannelMain, RuntimeFakeMessageChannelMain);
        assert.strictEqual(electronMainShim.MessageChannelMain, RuntimeFakeMessageChannelMain);
        assert.strictEqual(browserExports.MessageChannelMain, RuntimeFakeMessageChannelMain);
        assert.strictEqual(electron.webFrameMain, RuntimeFakeWebFrameMain);
        assert.strictEqual(electronMainShim.webFrameMain, RuntimeFakeWebFrameMain);
        assert.strictEqual(browserExports.webFrameMain, RuntimeFakeWebFrameMain);
        assert.strictEqual(typeof electron.webFrameMain.fromId, 'function');
        assert.strictEqual(typeof electron.webFrameMain.fromIdOrNull, 'function');
        assert.strictEqual(browserExports.nativeTheme, electron.nativeTheme);
        assert.strictEqual(electronMainShim.nativeTheme, electron.nativeTheme);
        assert.strictEqual(electron.net, RuntimeFakeNet);
        assert.strictEqual(electronMainShim.net, RuntimeFakeNet);
        assert.strictEqual(browserExports.net, RuntimeFakeNet);
        assert.strictEqual(typeof electron.net.request, 'function');
        assert.strictEqual(typeof electron.net.isOnline, 'function');
        assert.strictEqual(electron.utilityProcess, RuntimeFakeUtilityProcess);
        assert.strictEqual(electronMainShim.utilityProcess, RuntimeFakeUtilityProcess);
        assert.strictEqual(browserExports.utilityProcess, RuntimeFakeUtilityProcess);
        assert.strictEqual(typeof electron.utilityProcess.fork, 'function');
        assert.strictEqual(electron.systemPreferences, RuntimeFakeSystemPreferences);
        assert.strictEqual(electronMainShim.systemPreferences, RuntimeFakeSystemPreferences);
        assert.strictEqual(browserExports.systemPreferences, RuntimeFakeSystemPreferences);
        assert.strictEqual(typeof electron.systemPreferences.getAccentColor, 'function');
        assert.strictEqual(electron.contentTracing, RuntimeFakeContentTracing);
        assert.strictEqual(electronMainShim.contentTracing, RuntimeFakeContentTracing);
        assert.strictEqual(browserExports.contentTracing, RuntimeFakeContentTracing);
        assert.strictEqual(typeof electron.contentTracing.startRecording, 'function');
        assert.strictEqual(electron.crashReporter, RuntimeFakeCrashReporter);
        assert.strictEqual(electronMainShim.crashReporter, RuntimeFakeCrashReporter);
        assert.strictEqual(browserExports.crashReporter, RuntimeFakeCrashReporter);
        assert.strictEqual(typeof electron.crashReporter.start, 'function');
        assert.strictEqual(electron.TouchBar, RuntimeFakeTouchBar);
        assert.strictEqual(electronMainShim.TouchBar, RuntimeFakeTouchBar);
        assert.strictEqual(browserExports.TouchBar, RuntimeFakeTouchBar);
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
        delete require.cache[browserWindowPath];
        delete require.cache[webContentsPath];
        delete require.cache[ipcMainPath];
        delete require.cache[sessionPath];
        delete require.cache[webFrameMainPath];
    }

    console.log('PASS app-runtime-js-smoke');
}

async function runBrowserWindowWebContentsSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const Module = require('module');
    const originalLoad = Module._load;
    const electronShim = {
        app: {
            getAppPath() {
                return '/tmp/miniblink-browser-window-smoke';
            }
        }
    };
    const browserWindowPath = require.resolve('../electron/lib/browser/api/browser-window');
    const webContentsPath = require.resolve('../electron/lib/browser/api/web-contents');
    const ipcMainPath = require.resolve('../electron/lib/browser/api/ipc-main');
    const sessionPath = require.resolve('../electron/lib/browser/api/session');

    class FakeWebContents {
        constructor() {
            this._url = '';
            this._sent = [];
            this._css = [];
            this._devToolsOpened = false;
        }

        static fromId(id) {
            return FakeWebContents._byId.get(id) || null;
        }

        static getAllWebContents() {
            return Array.from(FakeWebContents._byId.values());
        }

        static getFocusedWebContents() {
            return FakeWebContents._focused || null;
        }

        getId() {
            return this.id;
        }

        _loadURL(targetURL) {
            this._url = targetURL;
            setImmediate(() => this.emit('did-finish-load'));
        }

        _getURL() {
            return this._url;
        }

        _insertCSS(css, options) {
            this._css.push({ css, options });
            return Promise.resolve('miniblink-css-key');
        }

        _send(frameId, sendToAll, channel, ...args) {
            this._sent.push({ frameId, sendToAll, channel, args });
            return true;
        }

        reload() {
            this._reloaded = true;
        }

        openDevTools() {
            this._devToolsOpened = true;
        }

        closeDevTools() {
            this._devToolsOpened = false;
        }

        isDevToolsOpened() {
            return this._devToolsOpened;
        }

        isDevToolsFocused() {
            return this._devToolsOpened;
        }

        toggleDevTools() {
            this._devToolsOpened = !this._devToolsOpened;
        }

        inspectElement() {
            this._inspected = true;
        }

        inspectServiceWorker() {
            this._inspectedServiceWorker = true;
        }

        showDefinitionForSelection() {
            this._showedDefinition = true;
        }

        capturePage() {
            return Promise.resolve(Buffer.from('capture'));
        }
    }
    FakeWebContents._byId = new Map();
    FakeWebContents._focused = null;

    class FakeBrowserWindow {
        constructor() {
            this.id = ++FakeBrowserWindow._nextId;
            this._title = '';
            this._webContents = new FakeWebContents();
            this._webContents.id = this.id * 10;
            FakeBrowserWindow._byId.set(this.id, this);
            FakeWebContents._byId.set(this._webContents.id, this._webContents);
            FakeBrowserWindow._focused = this;
            FakeWebContents._focused = this._webContents;
        }

        static fromId(id) {
            return FakeBrowserWindow._byId.get(id) || null;
        }

        static fromWebContents(contents) {
            for (const win of FakeBrowserWindow._byId.values()) {
                if (win._webContents === contents)
                    return win;
            }
            return null;
        }

        static getAllWindows() {
            return Array.from(FakeBrowserWindow._byId.values());
        }

        static getFocusedWindow() {
            return FakeBrowserWindow._focused || null;
        }

        _getWebContents() {
            return this._webContents;
        }

        _setTitle(title) {
            this._title = title;
        }

        getTitle() {
            return this._title;
        }

        _setTouchBar(model) {
            this._nativeTouchBarModel = model;
        }
    }
    FakeBrowserWindow._nextId = 0;
    FakeBrowserWindow._byId = new Map();
    FakeBrowserWindow._focused = null;

    class FakeSession {
        static fromPartition() {
            return new FakeSession();
        }
    }
    class FakeWebRequest {}
    class FakeDownloadItem {}

    delete require.cache[browserWindowPath];
    delete require.cache[webContentsPath];
    delete require.cache[ipcMainPath];
    delete require.cache[sessionPath];

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_web_contents')
            return { WebContents: FakeWebContents };
        if (name === 'electron_browser_browserwindow')
            return { BrowserWindow: FakeBrowserWindow };
        if (name === 'electron_browser_session')
            return { Session: FakeSession };
        if (name === 'electron_browser_webrequest')
            return { WebRequest: FakeWebRequest };
        if (name === 'electron_browser_downloaditem')
            return { DownloadItem: FakeDownloadItem };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    Module._load = function(request, parent, isMain) {
        if (request === 'electron')
            return electronShim;
        return originalLoad.call(this, request, parent, isMain);
    };

    try {
        const BrowserWindow = require('../electron/lib/browser/api/browser-window');
        const WebContents = require('../electron/lib/browser/api/web-contents');
        const win = new BrowserWindow();
        const contents = win.webContents;

        assert.strictEqual(BrowserWindow.getFocusedWindow(), win);
        assert.strictEqual(BrowserWindow.fromId(win.id), win);
        assert.strictEqual(BrowserWindow.fromWebContents(contents), win);
        assert.deepStrictEqual(BrowserWindow.getAllWindows(), [win]);
        assert.strictEqual(WebContents.getFocusedWebContents(), contents);
        assert.strictEqual(WebContents.fromId(contents.id), contents);
        assert.deepStrictEqual(WebContents.getAllWebContents(), [contents]);

        assert.strictEqual(contents.webContents, contents);
        assert.strictEqual(typeof contents.ipc.on, 'function');
        assert.strictEqual(typeof contents.ipc.handle, 'function');

        win.setTitle('MiniBlink BrowserWindow');
        assert.strictEqual(win.getTitle(), 'MiniBlink BrowserWindow');

        const TouchBar = require('../electron/lib/browser/api/touch-bar');
        let touchBarClicked = 0;
        let sliderValue = null;
        let segmentState = null;
        let scrubberSelected = null;
        let scrubberHighlighted = null;
        let groupClicked = 0;
        let popoverClicked = 0;
        const touchBar = new TouchBar({
            items: [
                new TouchBar.TouchBarButton({
                    label: 'Run',
                    image: Buffer.from('png-bytes'),
                    backgroundColor: '#336699',
                    click() { touchBarClicked++; }
                }),
                new TouchBar.TouchBarSlider({
                    label: 'Level',
                    minValue: 0,
                    maxValue: 10,
                    value: 5,
                    change(value) { sliderValue = value; }
                }),
                new TouchBar.TouchBarSegmentedControl({
                    mode: 'multiple',
                    selectedIndex: 1,
                    segments: [
                        { label: 'One', image: Buffer.from('seg-one') },
                        { label: 'Two', enabled: false }
                    ],
                    change(index, selected) { segmentState = { index, selected }; }
                }),
                new TouchBar.TouchBarScrubber({
                    items: [
                        { label: 'A', image: Buffer.from('scrub-a') },
                        'B'
                    ],
                    mode: 'fixed',
                    selectedStyle: 'outline',
                    overlayStyle: 'background',
                    showArrowButtons: true,
                    continuous: true,
                    selectedIndex: 0,
                    select(index) { scrubberSelected = index; },
                    highlight(index) { scrubberHighlighted = index; }
                }),
                new TouchBar.TouchBarGroup({
                    label: 'Group',
                    items: [
                        new TouchBar.TouchBarButton({
                            label: 'Nested',
                            click() { groupClicked++; }
                        })
                    ]
                }),
                new TouchBar.TouchBarPopover({
                    label: 'More',
                    icon: Buffer.from('popover-icon'),
                    showCloseButton: false,
                    items: [
                        new TouchBar.TouchBarButton({
                            label: 'Inside',
                            click() { popoverClicked++; }
                        })
                    ]
                })
            ]
        });
        win.setTouchBar(touchBar);
        assert.strictEqual(win._touchBar, touchBar);
        assert.strictEqual(win._nativeTouchBarModel.items[0].type, 'button');
        assert.strictEqual(win._nativeTouchBarModel.items[0].label, 'Run');
        assert.strictEqual(win._nativeTouchBarModel.items[0].image, 'data:image/png;base64,cG5nLWJ5dGVz');
        assert.strictEqual(win._nativeTouchBarModel.items[0].backgroundColor, '#336699');
        assert.strictEqual(win._nativeTouchBarModel.items[1].type, 'slider');
        assert.strictEqual(win._nativeTouchBarModel.items[1].value, 5);
        assert.strictEqual(win._nativeTouchBarModel.items[2].type, 'segmented-control');
        assert.strictEqual(win._nativeTouchBarModel.items[2].mode, 'multiple');
        assert.strictEqual(win._nativeTouchBarModel.items[2].segments[0].image, 'data:image/png;base64,c2VnLW9uZQ==');
        assert.strictEqual(win._nativeTouchBarModel.items[3].type, 'scrubber');
        assert.strictEqual(win._nativeTouchBarModel.items[3].items[0].image, 'data:image/png;base64,c2NydWItYQ==');
        assert.strictEqual(win._nativeTouchBarModel.items[3].selectedStyle, 'outline');
        assert.strictEqual(win._nativeTouchBarModel.items[3].overlayStyle, 'background');
        assert.strictEqual(win._nativeTouchBarModel.items[3].showArrowButtons, true);
        assert.strictEqual(win._nativeTouchBarModel.items[3].continuous, true);
        assert.strictEqual(win._nativeTouchBarModel.items[3].selectedIndex, 0);
        assert.strictEqual(win._nativeTouchBarModel.items[4].type, 'group');
        assert.strictEqual(win._nativeTouchBarModel.items[4].label, 'Group');
        assert.strictEqual(win._nativeTouchBarModel.items[4].items[0].label, 'Nested');
        assert.strictEqual(win._nativeTouchBarModel.items[5].type, 'popover');
        assert.strictEqual(win._nativeTouchBarModel.items[5].image, 'data:image/png;base64,cG9wb3Zlci1pY29u');
        assert.strictEqual(win._nativeTouchBarModel.items[5].showCloseButton, false);
        assert.strictEqual(win._dispatchTouchBarAction(JSON.stringify({
            id: win._nativeTouchBarModel.items[0].id,
            type: 'click'
        })), true);
        assert.strictEqual(touchBarClicked, 1);
        assert.strictEqual(win._dispatchTouchBarAction({
            id: win._nativeTouchBarModel.items[1].id,
            type: 'change',
            value: 7
        }), true);
        assert.strictEqual(sliderValue, 7);
        assert.strictEqual(win._dispatchTouchBarAction({
            id: win._nativeTouchBarModel.items[2].id,
            type: 'change',
            selectedIndex: 0,
            isSelected: false
        }), true);
        assert.deepStrictEqual(segmentState, { index: 0, selected: false });
        assert.strictEqual(win._dispatchTouchBarAction({
            id: win._nativeTouchBarModel.items[3].id,
            type: 'select',
            selectedIndex: 1
        }), true);
        assert.strictEqual(scrubberSelected, 1);
        assert.strictEqual(win._dispatchTouchBarAction({
            id: win._nativeTouchBarModel.items[3].id,
            type: 'highlight',
            highlightedIndex: 0
        }), true);
        assert.strictEqual(scrubberHighlighted, 0);
        assert.strictEqual(win._dispatchTouchBarAction({
            id: win._nativeTouchBarModel.items[4].items[0].id,
            type: 'click'
        }), true);
        assert.strictEqual(groupClicked, 1);
        assert.strictEqual(win._dispatchTouchBarAction({
            id: win._nativeTouchBarModel.items[5].items[0].id,
            type: 'click'
        }), true);
        assert.strictEqual(popoverClicked, 1);
        win.setTouchBar(null);
        assert.strictEqual(win._touchBar, null);
        assert.strictEqual(win._nativeTouchBarModel, null);

        await win.loadURL('https://example.test/page');
        assert.strictEqual(win.getURL(), 'https://example.test/page');

        await win.loadFile('index.html');
        assert.ok(win.getURL().startsWith('file:///tmp/miniblink-browser-window-smoke/index.html'));

        assert.strictEqual(await contents.insertCSS('body { color: red; }', { cssOrigin: 'author' }), 'miniblink-css-key');
        assert.deepStrictEqual(contents._css[0], {
            css: 'body { color: red; }',
            options: { cssOrigin: 'author' }
        });

        assert.strictEqual(win.send('channel', 1, 'two'), true);
        assert.deepStrictEqual(contents._sent[0], {
            frameId: 0,
            sendToAll: false,
            channel: 'channel',
            args: [1, 'two']
        });

        win.openDevTools();
        assert.strictEqual(win.isDevToolsOpened(), true);
        win.closeDevTools();
        assert.strictEqual(win.isDevToolsOpened(), false);
        assert.strictEqual((await win.capturePage()).toString(), 'capture');

        console.log('PASS browser-window-webcontents-js-smoke');
    } finally {
        Module._load = originalLoad;
        process._linkedBinding = originalLinkedBinding;
        delete require.cache[browserWindowPath];
        delete require.cache[webContentsPath];
        delete require.cache[ipcMainPath];
        delete require.cache[sessionPath];
    }
}

function runSessionApiSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const sessionPath = require.resolve('../electron/lib/browser/api/session');
    delete require.cache[sessionPath];

    class FakeWebRequest {
        constructor() {
            this.callbacks = Object.create(null);
        }

        setCallback(name, args) {
            const values = Array.from(args);
            this.callbacks[name] = {
                filter: values.length === 2 ? values[0] : null,
                callback: values.length === 2 ? values[1] : values[0]
            };
        }

        onBeforeSendHeaders() { this.setCallback('onBeforeSendHeaders', arguments); }
        onSendHeaders() { this.setCallback('onSendHeaders', arguments); }
        onBeforeRedirect() { this.setCallback('onBeforeRedirect', arguments); }
        onHeadersReceived() { this.setCallback('onHeadersReceived', arguments); }
        onResponseStarted() { this.setCallback('onResponseStarted', arguments); }
        onCompleted() { this.setCallback('onCompleted', arguments); }
        onErrorOccurred() { this.setCallback('onErrorOccurred', arguments); }
        onBeforeRequest() { this.setCallback('onBeforeRequest', arguments); }
    }

    class FakeSession {
        constructor(partition) {
            this.partition = partition || '';
            this.webRequest = new FakeWebRequest();
            this._preloads = [];
            this._downloadPath = '';
            this.permissionRequestHandler = null;
            this.permissionCheckHandler = null;
            this.devicePermissionHandler = null;
        }

        static fromPartition(partition) {
            const key = partition || '';
            if (!FakeSession.sessions.has(key))
                FakeSession.sessions.set(key, new FakeSession(key));
            return FakeSession.sessions.get(key);
        }

        setPreloads(paths) {
            this._preloads = paths.slice();
        }

        getPreloads() {
            return this._preloads.slice();
        }

        setDownloadPath(path) {
            this._downloadPath = path;
        }

        setPermissionRequestHandler(callback) {
            this.permissionRequestHandler = typeof callback === 'function' ? callback : null;
        }

        setPermissionCheckHandler(callback) {
            this.permissionCheckHandler = typeof callback === 'function' ? callback : null;
        }

        setDevicePermissionHandler(callback) {
            this.devicePermissionHandler = typeof callback === 'function' ? callback : null;
        }
    }
    FakeSession.sessions = new Map();

    class FakeDownloadItem {
        constructor() {
            this._savePath = '';
            this._saveDialogOptions = {};
            this._paused = false;
            this._state = 'progressing';
        }

        setSavePath(path) {
            this._savePath = path;
        }

        getSavePath() {
            return this._savePath;
        }

        setSaveDialogOptions(options) {
            this._saveDialogOptions = options || {};
        }

        getSaveDialogOptions() {
            return this._saveDialogOptions;
        }

        pause() {
            this._paused = true;
        }

        isPaused() {
            return this._paused;
        }

        resume() {
            this._paused = false;
        }

        canResume() {
            return true;
        }

        cancel() {
            if (this._state === 'cancelled')
                return;
            this._state = 'cancelled';
            this.emit('updated', 'cancelled');
            this.emit('done', 'cancelled', 'cancelled');
        }

        cancels() {
            return this.cancel();
        }

        getState() {
            return this._state;
        }

        getURLChain() {
            return [];
        }
    }

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_session')
            return { Session: FakeSession };
        if (name === 'electron_browser_webrequest')
            return { WebRequest: FakeWebRequest };
        if (name === 'electron_browser_downloaditem')
            return { DownloadItem: FakeDownloadItem };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    try {
        const session = require('../electron/lib/browser/api/session').session;

        assert.strictEqual(session, FakeSession);
        assert.strictEqual(session.defaultSession, FakeSession.fromPartition(''));
        assert.strictEqual(session.defaultSession.partition, '');
        assert.strictEqual(typeof session.defaultSession.on, 'function');

        const profileSession = session.fromPartition('persist:profile');
        assert.strictEqual(profileSession, session.fromPartition('persist:profile'));
        assert.notStrictEqual(profileSession, session.defaultSession);

        profileSession.setPreloads(['/tmp/preload-a.js', '/tmp/preload-b.js']);
        assert.deepStrictEqual(profileSession.getPreloads(), ['/tmp/preload-a.js', '/tmp/preload-b.js']);
        profileSession.setDownloadPath('/tmp/downloads');
        assert.strictEqual(profileSession._downloadPath, '/tmp/downloads');

        function permissionRequestHandler() {}
        function permissionCheckHandler() { return true; }
        function devicePermissionHandler() { return true; }
        profileSession.setPermissionRequestHandler(permissionRequestHandler);
        profileSession.setPermissionCheckHandler(permissionCheckHandler);
        profileSession.setDevicePermissionHandler(devicePermissionHandler);
        assert.strictEqual(profileSession.permissionRequestHandler, permissionRequestHandler);
        assert.strictEqual(profileSession.permissionCheckHandler, permissionCheckHandler);
        assert.strictEqual(profileSession.devicePermissionHandler, devicePermissionHandler);
        profileSession.setPermissionRequestHandler(null);
        profileSession.setPermissionCheckHandler(null);
        profileSession.setDevicePermissionHandler(null);
        assert.strictEqual(profileSession.permissionRequestHandler, null);
        assert.strictEqual(profileSession.permissionCheckHandler, null);
        assert.strictEqual(profileSession.devicePermissionHandler, null);

        const filter = { urls: ['*://example.test/*'] };
        const webRequestMethods = [
            'onBeforeSendHeaders',
            'onSendHeaders',
            'onBeforeRedirect',
            'onHeadersReceived',
            'onResponseStarted',
            'onCompleted',
            'onErrorOccurred',
            'onBeforeRequest'
        ];
        for (const method of webRequestMethods) {
            const callback = function() {};
            profileSession.webRequest[method](filter, callback);
            assert.strictEqual(profileSession.webRequest.callbacks[method].filter, filter);
            assert.strictEqual(profileSession.webRequest.callbacks[method].callback, callback);
        }

        const downloadItem = new FakeDownloadItem();
        assert.strictEqual(typeof downloadItem.on, 'function');
        downloadItem.setSavePath('/tmp/file.bin');
        assert.strictEqual(downloadItem.getSavePath(), '/tmp/file.bin');
        downloadItem.setSaveDialogOptions({ title: 'Save file' });
        assert.deepStrictEqual(downloadItem.getSaveDialogOptions(), { title: 'Save file' });
        downloadItem.pause();
        assert.strictEqual(downloadItem.isPaused(), true);
        downloadItem.resume();
        assert.strictEqual(downloadItem.canResume(), true);
        assert.strictEqual(downloadItem.isPaused(), false);

        const downloadEvents = [];
        downloadItem.on('updated', function(state) { downloadEvents.push(['updated', state]); });
        downloadItem.on('done', function(eventState, finalState) { downloadEvents.push(['done', eventState, finalState]); });
        downloadItem.cancel();
        downloadItem.cancels();
        assert.strictEqual(downloadItem.getState(), 'cancelled');
        assert.deepStrictEqual(downloadEvents, [
            ['updated', 'cancelled'],
            ['done', 'cancelled', 'cancelled']
        ]);

        console.log('PASS session-js-smoke');
    } finally {
        process._linkedBinding = originalLinkedBinding;
        delete require.cache[sessionPath];
    }
}

async function runIpcMainApiSmoke() {
    const ipcMainPath = require.resolve('../electron/lib/browser/api/ipc-main');
    delete require.cache[ipcMainPath];

    try {
        const ipcMain = require('../electron/lib/browser/api/ipc-main');
        const scopedIpcMain = ipcMain.createIpcMain();

        const messageEvents = [];
        scopedIpcMain.on('plain-message', function(event, value) {
            messageEvents.push([event.innnerChannel, value]);
        });
        scopedIpcMain.emit('plain-message', { innnerChannel: 'ipc-render-invoke' }, 'skip');
        scopedIpcMain.emit('plain-message', { innnerChannel: 'ipc-message' }, 'deliver');
        assert.deepStrictEqual(messageEvents, [['ipc-message', 'deliver']]);

        const replies = [];
        const event = {
            innnerChannel: 'ipc-render-invoke',
            sender: {
                send(channel, value) {
                    replies.push([channel, value]);
                }
            }
        };

        scopedIpcMain.handle('sync-channel', function(invokeEvent, value) {
            assert.strictEqual(invokeEvent, event);
            return value + 1;
        });
        assert.throws(function() {
            scopedIpcMain.handle('sync-channel', function() {});
        }, /second handler/);
        scopedIpcMain.emit('sync-channel', event, 41);
        await new Promise(function(resolve) { setImmediate(resolve); });
        assert.deepStrictEqual(replies, [['ipc-main-handle-reply-sync-channel', 42]]);

        scopedIpcMain.removeHandler('sync-channel');
        assert.strictEqual(scopedIpcMain.m_invokeHandlers.has('sync-channel'), false);
        scopedIpcMain.emit('sync-channel', event, 1);
        await new Promise(function(resolve) { setImmediate(resolve); });
        assert.deepStrictEqual(replies, [['ipc-main-handle-reply-sync-channel', 42]]);

        scopedIpcMain.handle('async-channel', function(invokeEvent, value) {
            return Promise.resolve(value.toUpperCase());
        });
        scopedIpcMain.emit('async-channel', event, 'ok');
        await new Promise(function(resolve) { setImmediate(resolve); });
        assert.deepStrictEqual(replies[1], ['ipc-main-handle-reply-async-channel', 'OK']);

        scopedIpcMain.removeHandler('missing-channel');
        assert.throws(function() {
            scopedIpcMain.handle('bad-channel', 'not a function');
        }, /Expected handler/);

        let errorEventCount = 0;
        ipcMain.emit('error');
        ipcMain.on('error', function() { errorEventCount++; });
        ipcMain.emit('error', { innnerChannel: 'ipc-message' });
        assert.strictEqual(errorEventCount, 1);

        console.log('PASS ipc-main-js-smoke');
    } finally {
        delete require.cache[ipcMainPath];
    }
}

function runMessageChannelMainSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const channelPath = require.resolve('../electron/lib/browser/api/message-channel-main');
    const portPath = require.resolve('../electron/lib/browser/api/message-port-main');
    delete require.cache[channelPath];
    delete require.cache[portPath];

    class FakeInternalPort {
        constructor(name) {
            this.name = name;
            this.started = false;
            this.closed = false;
            this.sent = [];
        }

        start() {
            this.started = true;
            return this.name + ':started';
        }

        close() {
            this.closed = true;
            return this.name + ':closed';
        }

        postMessage(message, ports) {
            this.sent.push({ message, ports });
            return this.name + ':posted';
        }
    }

    const createdPairs = [];
    process._linkedBinding = function(name) {
        if (name === 'electron_browser_message_port') {
            return {
                createPair() {
                    const pair = {
                        port1: new FakeInternalPort('port1'),
                        port2: new FakeInternalPort('port2')
                    };
                    createdPairs.push(pair);
                    return pair;
                }
            };
        }
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    try {
        const { MessageChannelMain } = require('../electron/lib/browser/api/message-channel-main');
        const channel = new MessageChannelMain();

        assert.strictEqual(createdPairs.length, 1);
        assert.strictEqual(typeof channel.port1.on, 'function');
        assert.strictEqual(typeof channel.port2.emit, 'function');
        assert.strictEqual(channel.port1.start(), 'port1:started');
        assert.strictEqual(channel.port1.close(), 'port1:closed');
        assert.strictEqual(createdPairs[0].port1.started, true);
        assert.strictEqual(createdPairs[0].port1.closed, true);

        assert.strictEqual(channel.port1.postMessage({ value: 1 }, [channel.port2]), 'port1:posted');
        assert.deepStrictEqual(createdPairs[0].port1.sent[0], {
            message: { value: 1 },
            ports: [createdPairs[0].port2]
        });

        assert.strictEqual(channel.port2.postMessage('plain'), 'port2:posted');
        assert.deepStrictEqual(createdPairs[0].port2.sent[0], { message: 'plain', ports: undefined });

        const received = [];
        channel.port1.on('message', function(event) {
            received.push(event);
        });
        createdPairs[0].port1.emit('message', {
            data: 'payload',
            ports: [new FakeInternalPort('received-port')]
        });
        assert.strictEqual(received.length, 1);
        assert.strictEqual(received[0].data, 'payload');
        assert.strictEqual(typeof received[0].ports[0].postMessage, 'function');

        console.log('PASS message-channel-main-js-smoke');
    } finally {
        process._linkedBinding = originalLinkedBinding;
        delete require.cache[channelPath];
        delete require.cache[portPath];
    }
}

function runWebFrameMainSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const webFrameMainPath = require.resolve('../electron/lib/browser/api/web-frame-main');
    const portPath = require.resolve('../electron/lib/browser/api/message-port-main');
    delete require.cache[webFrameMainPath];
    delete require.cache[portPath];

    const createdFrames = [];
    class FakeWebFrameMain {
        constructor(frameId) {
            this.frameId = frameId;
            this.sent = [];
            this.posted = [];
            createdFrames.push(this);
        }

        _send(internal, channel, ...args) {
            this.sent.push({ internal, channel, args });
            return 'sent:' + channel;
        }

        _postMessage(channel, message, transfer) {
            this.posted.push({ channel, message, transfer });
            return 'posted:' + channel;
        }
    }

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_web_frame_main') {
            return {
                WebFrameMain: FakeWebFrameMain,
                fromId(processId, frameId) {
                    return new FakeWebFrameMain(frameId);
                },
                fromIdOrNull(processId, frameId) {
                    return frameId === 0 ? null : new FakeWebFrameMain(frameId);
                }
            };
        }
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    try {
        const MessagePortMain = require('../electron/lib/browser/api/message-port-main');
        const { WebFrameMain, webFrameMain } = require('../electron/lib/browser/api/web-frame-main');
        assert.strictEqual(WebFrameMain, FakeWebFrameMain);
        assert.strictEqual(typeof WebFrameMain.prototype.on, 'function');
        assert.strictEqual(typeof webFrameMain.fromId, 'function');
        assert.strictEqual(typeof webFrameMain.fromIdOrNull, 'function');

        const frame = webFrameMain.fromId(1, 42);
        assert.strictEqual(frame.frameId, 42);
        assert.strictEqual(frame.send('ipc-channel', 'a', 1), 'sent:ipc-channel');
        assert.deepStrictEqual(frame.sent[0], {
            internal: false,
            channel: 'ipc-channel',
            args: ['a', 1]
        });

        assert.strictEqual(frame._sendInternal('internal-channel', { ok: true }), 'sent:internal-channel');
        assert.deepStrictEqual(frame.sent[1], {
            internal: true,
            channel: 'internal-channel',
            args: [{ ok: true }]
        });

        assert.throws(function() { frame.send(null); }, /Missing required channel argument/);
        assert.throws(function() { frame._sendInternal({}); }, /Missing required channel argument/);

        const rawPort = { start() {}, close() {}, postMessage() {} };
        const wrappedPort = new MessagePortMain(rawPort);
        assert.strictEqual(frame.postMessage('port-channel', { data: 1 }, [wrappedPort]), undefined);
        assert.deepStrictEqual(frame.posted[0], {
            channel: 'port-channel',
            message: { data: 1 },
            transfer: [rawPort]
        });

        assert.strictEqual(webFrameMain.fromIdOrNull(1, 0), null);
        const nullableFrame = webFrameMain.fromIdOrNull(1, 7);
        assert.strictEqual(nullableFrame.frameId, 7);
        assert.strictEqual(createdFrames.length, 2);

        console.log('PASS web-frame-main-js-smoke');
    } finally {
        process._linkedBinding = originalLinkedBinding;
        delete require.cache[webFrameMainPath];
        delete require.cache[portPath];
    }
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

function runBrowserViewSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const modulePath = require.resolve('../electron/lib/browser/api/browser-view');
    const cachedModule = require.cache[modulePath];
    delete require.cache[modulePath];

    class FakeBrowserView {
        constructor() {
            this.boundsCalls = [];
            this.backgroundCalls = [];
            this.autoResizeCalls = [];
            this._webContents = {
                initCount: 0,
                _init() {
                    this.initCount++;
                }
            };
        }

        _getWebContents() {
            return this._webContents;
        }

        _setBounds(x, y, width, height) {
            this.boundsCalls.push({ x, y, width, height });
        }

        _setBackgroundColor(color) {
            this.backgroundCalls.push(color);
        }

        _setAutoResize(width, height, horizontal, vertical) {
            this.autoResizeCalls.push({ width, height, horizontal, vertical });
        }
    }

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_browserview')
            return { BrowserView: FakeBrowserView };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    try {
        const BrowserView = require('../electron/lib/browser/api/browser-view');
        const view = new BrowserView();

        assert.strictEqual(BrowserView, FakeBrowserView);
        assert.strictEqual(view.webContents, view._webContents);
        assert.strictEqual(view._webContents.initCount, 1);

        assert.deepStrictEqual(view.getBounds(), { x: 0, y: 0, width: 1, height: 1 });
        view.setBounds({ x: 10, y: 20, width: 300, height: 200 });
        assert.deepStrictEqual(view.getBounds(), { x: 10, y: 20, width: 300, height: 200 });
        assert.deepStrictEqual(view.boundsCalls[0], { x: 10, y: 20, width: 300, height: 200 });

        view.setBounds({ width: 640, height: 480 });
        assert.deepStrictEqual(view.getBounds(), { x: 0, y: 0, width: 640, height: 480 });
        assert.deepStrictEqual(view.boundsCalls[1], { x: 0, y: 0, width: 640, height: 480 });

        view.setBackgroundColor('#336699');
        assert.strictEqual(view.getBackgroundColor(), '#336699');
        assert.strictEqual(view.backgroundCalls[0], 0x996633);
        view.setBackgroundColor('#abc');
        assert.strictEqual(view.backgroundCalls[1], 0xccbbaa);
        assert.throws(function() { view.setBackgroundColor('red'); }, /color must be/);

        assert.strictEqual(typeof view.setAutoResize, 'function');
        view.setAutoResize({ width: true, vertical: 1 });
        assert.deepStrictEqual(view._autoResize, {
            width: true,
            height: false,
            horizontal: false,
            vertical: true
        });
        assert.deepStrictEqual(view.autoResizeCalls[0], {
            width: true,
            height: false,
            horizontal: false,
            vertical: true
        });
        assert.throws(function() { view.setAutoResize('bad'); }, /options must be an object/);

        console.log('PASS browser-view-js-smoke');
    } finally {
        process._linkedBinding = originalLinkedBinding;
        if (cachedModule)
            require.cache[modulePath] = cachedModule;
        else
            delete require.cache[modulePath];
    }
}

async function runProtocolSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const modulePath = require.resolve('../electron/lib/browser/api/protocol');
    const cachedModule = require.cache[modulePath];
    delete require.cache[modulePath];

    class FakeProtocol {
        constructor(receiver) {
            this.receiver = receiver;
            this.registered = new Map();
            this.standardSchemes = [];
            this.privilegedSchemes = [];
            this.finished = [];
            FakeProtocol.latest = this;
        }

        registerStandardSchemes(schemes) {
            this.standardSchemes.push(schemes.slice());
        }

        registerSchemesAsPrivileged(schemes) {
            this.privilegedSchemes.push(JSON.parse(JSON.stringify(schemes)));
        }

        _registerProtocol(scheme, id, type, registerWithBlink) {
            if (this.registered.has(scheme))
                return false;
            this.registered.set(scheme, { id, type, registerWithBlink });
            return true;
        }

        _unregisterProtocol(scheme) {
            this.registered.delete(scheme);
        }

        _isProtocolHandled(scheme) {
            return this.registered.has(scheme);
        }

        onHandlerFinish(request, nativeCallbackInfo) {
            this.finished.push({ request, nativeCallbackInfo });
        }
    }

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_protocol')
            return { Protocol: FakeProtocol };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    globalThis.__electronProtocolSkipBlinkRegistrationForTesting = true;

    try {
        const { protocol } = require('../electron/lib/browser/api/protocol');

        assert.strictEqual(typeof protocol.registerFileProtocol, 'function');
        assert.strictEqual(typeof protocol.registerStringProtocol, 'function');
        assert.strictEqual(typeof protocol.registerBufferProtocol, 'function');
        assert.strictEqual(typeof protocol.registerHttpProtocol, 'function');
        assert.strictEqual(typeof protocol.interceptFileProtocol, 'function');
        assert.strictEqual(typeof protocol.interceptStringProtocol, 'function');
        assert.strictEqual(typeof protocol.interceptBufferProtocol, 'function');
        assert.strictEqual(typeof protocol.interceptHttpProtocol, 'function');
        assert.strictEqual(typeof protocol.registerStandardSchemes, 'function');
        assert.strictEqual(typeof protocol.registerSchemesAsPrivileged, 'function');
        assert.strictEqual(typeof protocol.registerStreamProtocol, 'function');
        assert.strictEqual(typeof protocol.interceptStreamProtocol, 'function');

        const instance = FakeProtocol.latest;
        let completionError = 'not-called';
        protocol.registerFileProtocol('mbfile', function() {}, function(error) { completionError = error; });
        assert.strictEqual(completionError, null);
        assert.deepStrictEqual(instance.registered.get('mbfile'), {
            id: 1,
            type: 'file',
            registerWithBlink: false
        });

        protocol.registerStringProtocol('mbstring', function(request, callback) {
            callback({ data: 'ok', mimeType: 'text/plain' });
        });
        assert.strictEqual(instance.registered.get('mbstring').type, 'string');

        let duplicateMessage = '';
        protocol.registerStringProtocol('mbstring', function() {}, function(error) {
            duplicateMessage = error ? error.message : '';
        });
        assert.strictEqual(duplicateMessage, 'The scheme has been registered');

        protocol.registerBufferProtocol('mbbuffer', function() {});
        assert.strictEqual(instance.registered.get('mbbuffer').type, 'buffer');
        protocol.registerHttpProtocol('mbhttp', function() {});
        assert.strictEqual(instance.registered.get('mbhttp').type, 'http');

        let handled = null;
        protocol.isProtocolHandled('mbhttp', function(value) { handled = value; });
        assert.strictEqual(handled, true);
        protocol.unregisterProtocol('mbhttp');
        protocol.isProtocolHandled('mbhttp', function(value) { handled = value; });
        assert.strictEqual(handled, false);

        protocol.interceptFileProtocol('mbfile2', function() {});
        assert.strictEqual(instance.registered.get('mbfile2').type, 'file');
        protocol.interceptStringProtocol('mbstring2', function() {});
        assert.strictEqual(instance.registered.get('mbstring2').type, 'string');
        protocol.interceptBufferProtocol('mbbuffer2', function() {});
        assert.strictEqual(instance.registered.get('mbbuffer2').type, 'buffer');
        protocol.interceptHttpProtocol('mbhttp2', function() {});
        assert.strictEqual(instance.registered.get('mbhttp2').type, 'http');
        protocol.uninterceptProtocol('mbhttp2');
        assert.strictEqual(instance.registered.has('mbhttp2'), false);

        const { Readable } = require('stream');
        protocol.registerStreamProtocol('mbstream', function(request, callback) {
            callback({
                data: Readable.from(['hello ', Buffer.from('stream')]),
                mimeType: 'text/plain'
            });
        });
        assert.strictEqual(instance.registered.get('mbstream').type, 'buffer');
        protocol.interceptStreamProtocol('mbstream2', function(request, callback) {
            callback(Readable.from([Buffer.from('intercept')]));
        });
        assert.strictEqual(instance.registered.get('mbstream2').type, 'buffer');

        protocol.registerStandardSchemes(['mbstandard']);
        assert.deepStrictEqual(instance.standardSchemes, [['mbstandard']]);
        protocol.registerSchemesAsPrivileged([
            { scheme: 'mbpriv', privileges: { secure: true, standard: true, bypassCSP: true } }
        ]);
        assert.deepStrictEqual(instance.privilegedSchemes, [
            [{ scheme: 'mbpriv', privileges: { secure: true, standard: true, bypassCSP: true } }]
        ]);

        instance.receiver(2, { url: 'mbstring://host/path' }, 42n);
        assert.deepStrictEqual(instance.finished, [
            { request: { data: 'ok', mimeType: 'text/plain' }, nativeCallbackInfo: 42n }
        ]);
        instance.receiver(instance.registered.get('mbstream').id, { url: 'mbstream://host/path' }, 43n);
        await new Promise(function(resolve, reject) {
            const deadline = Date.now() + 1000;
            function poll() {
                if (instance.finished.length > 1)
                    return resolve();
                if (Date.now() > deadline)
                    return reject(new Error('stream protocol response timed out'));
                setImmediate(poll);
            }
            poll();
        });
        assert.strictEqual(instance.finished[1].request.mimeType, 'text/plain');
        assert.strictEqual(instance.finished[1].request.data.toString(), 'hello stream');
        assert.strictEqual(instance.finished[1].nativeCallbackInfo, 43n);

        console.log('PASS protocol-js-smoke');
    } finally {
        process._linkedBinding = originalLinkedBinding;
        delete globalThis.__electronProtocolSkipBlinkRegistrationForTesting;
        if (cachedModule)
            require.cache[modulePath] = cachedModule;
        else
            delete require.cache[modulePath];
    }
}

async function runSystemPreferencesSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const modulePath = require.resolve('../electron/lib/browser/api/system-preferences');
    const cachedModule = require.cache[modulePath];
    delete require.cache[modulePath];

    const calls = [];
    const fakeBinding = {
        defaults: Object.create(null),
        getAccentColor() {
            calls.push(['getAccentColor']);
            return 'AABBCC';
        },
        getColor(color) {
            calls.push(['getColor', color]);
            if (color === 'label')
                return '101112';
            throw new Error('Unknown system color');
        },
        isDarkMode() {
            calls.push(['isDarkMode']);
            return true;
        },
        isSwipeTrackingFromScrollEventsEnabled() {
            calls.push(['isSwipeTrackingFromScrollEventsEnabled']);
            return false;
        },
        getUserDefault(name, type) {
            calls.push(['getUserDefault', name, type]);
            return this.defaults[name] === undefined ? null : this.defaults[name];
        },
        setUserDefault(name, type, value) {
            calls.push(['setUserDefault', name, type, value]);
            this.defaults[name] = value;
        },
        removeUserDefault(name) {
            calls.push(['removeUserDefault', name]);
            delete this.defaults[name];
        },
        isTrustedAccessibilityClient(prompt) {
            calls.push(['isTrustedAccessibilityClient', prompt]);
            return !!prompt;
        },
        getMediaAccessStatus(mediaType) {
            calls.push(['getMediaAccessStatus', mediaType]);
            return mediaType === 'camera' ? 'granted' : 'not-determined';
        },
        askForMediaAccess(mediaType, callback) {
            calls.push(['askForMediaAccess', mediaType]);
            setImmediate(function() { callback(mediaType === 'microphone'); });
        }
    };

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_system_preferences')
            return fakeBinding;
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    try {
        const systemPreferences = require('../electron/lib/browser/api/system-preferences');

        assert.strictEqual(systemPreferences.getAccentColor(), 'AABBCC');
        assert.strictEqual(systemPreferences.getColor('label'), '101112');
        assert.throws(function() { systemPreferences.getColor(''); }, /color/);
        assert.strictEqual(systemPreferences.isDarkMode(), true);
        assert.strictEqual(systemPreferences.isSwipeTrackingFromScrollEventsEnabled(), false);

        systemPreferences.setUserDefault('mini.test.string', 'string', 'value');
        assert.strictEqual(systemPreferences.getUserDefault('mini.test.string', 'string'), 'value');
        systemPreferences.setUserDefault('mini.test.bool', 'boolean', true);
        assert.strictEqual(systemPreferences.getUserDefault('mini.test.bool', 'boolean'), true);
        systemPreferences.removeUserDefault('mini.test.string');
        assert.strictEqual(systemPreferences.getUserDefault('mini.test.string', 'string'), null);
        assert.throws(function() { systemPreferences.getUserDefault('', 'string'); }, /name/);
        assert.throws(function() { systemPreferences.getUserDefault('name', 'object'); }, /type/);

        assert.strictEqual(systemPreferences.isTrustedAccessibilityClient(true), true);
        assert.strictEqual(systemPreferences.getMediaAccessStatus('camera'), 'granted');
        assert.strictEqual(systemPreferences.getMediaAccessStatus('microphone'), 'not-determined');
        assert.throws(function() { systemPreferences.getMediaAccessStatus('screen'); }, /mediaType/);
        assert.strictEqual(await systemPreferences.askForMediaAccess('microphone'), true);
        assert.strictEqual(calls.some(function(call) { return call[0] === 'askForMediaAccess' && call[1] === 'microphone'; }), true);

        console.log('PASS system-preferences-js-smoke');
    } finally {
        process._linkedBinding = originalLinkedBinding;
        if (cachedModule)
            require.cache[modulePath] = cachedModule;
        else
            delete require.cache[modulePath];
    }
}

async function runContentTracingSmoke() {
    const fs = require('fs');
    const os = require('os');
    const path = require('path');
    const modulePath = require.resolve('../electron/lib/browser/api/content-tracing');
    const cachedModule = require.cache[modulePath];
    const originalLinkedBinding = process._linkedBinding;
    const nativeEvents = [];
    let nativeRecording = false;
    let nativeStartConfig = null;
    let perfettoTraceDataBase64 = '';
    let perfettoStats = {
        initialized: false,
        recording: false,
        startedCount: 0,
        eventCount: 0,
        droppedEventCount: 0,
        lastTraceSize: 0,
        lastTracePacketCountHint: 0
    };

    process._linkedBinding = function(name) {
        if (name === 'electron_common_content_tracing') {
            return {
                startRecording(categoryFilter, traceOptions, heapProfiling) {
                    nativeRecording = true;
                    perfettoTraceDataBase64 = '';
                    nativeStartConfig = {
                        categoryFilter,
                        traceOptions,
                        heapProfiling
                    };
                    perfettoStats = {
                        initialized: true,
                        recording: true,
                        startedCount: perfettoStats.startedCount + 1,
                        eventCount: 0,
                        droppedEventCount: 0,
                        lastTraceSize: 0,
                        lastTracePacketCountHint: 0,
                        lastTraceStatsSuccess: false,
                        lastTraceStatsSize: 0,
                        traceStatsProducersConnected: 0,
                        traceStatsProducersSeen: 0,
                        traceStatsDataSourcesRegistered: 0,
                        traceStatsDataSourcesSeen: 0,
                        traceStatsTracingSessions: 0,
                        traceStatsTotalBuffers: 0,
                        traceStatsBytesWritten: 0,
                        traceStatsChunksWritten: 0,
                        tracePacketCount: 0,
                        trackDescriptorPacketCount: 0,
                        processDescriptorCount: 0,
                        threadDescriptorCount: 0,
                        trackEventPacketCount: 0,
                        trackEventSliceBeginCount: 0,
                        trackEventSliceEndCount: 0,
                        trackEventInstantCount: 0,
                        trackEventCounterCount: 0,
                        trackEventCounterValueCount: 0,
                        trackEventNamedCount: 0,
                        trackEventDirectNameCount: 0,
                        trackEventInternedNameCount: 0,
                        trackEventTrackNameCount: 0,
                        trackEventNames: '',
                        lastServiceStateSuccess: false,
                        lastServiceStateSize: 0,
                        serviceStateProducerCount: 0,
                        serviceStateDataSourceCount: 0,
                        serviceStateTracingSessionCount: 0,
                        serviceStateSupportsTracingSessions: false,
                        serviceStateNumSessions: 0,
                        serviceStateNumSessionsStarted: 0
                    };
                },
                stopRecording() {
                    nativeRecording = false;
                    const outputEvents = nativeEvents.slice();
                    if (nativeStartConfig && nativeStartConfig.heapProfiling) {
                        outputEvents.push({
                            cat: 'disabled-by-default-memory-infra',
                            name: 'HeapProfiler.session',
                            ph: 'i',
                            args: {
                                samplingRate: nativeStartConfig.heapProfiling.samplingRate,
                                stackMode: nativeStartConfig.heapProfiling.stackMode,
                                sampleCount: 0
                            }
                        });
                    }
                    perfettoStats.recording = false;
                    const mirroredEvents = nativeEvents.filter(function(event) {
                        return event.ph === 'i' || event.ph === 'C';
                    });
                    const trackEventNames = mirroredEvents.map(function(event) {
                        return event.name;
                    }).join(',');
                    perfettoTraceDataBase64 = Buffer.from('miniblink-perfetto-track-event-smoke').toString('base64');
                    perfettoStats.lastTraceSize = Buffer.from(perfettoTraceDataBase64, 'base64').length;
                    perfettoStats.lastTracePacketCountHint = mirroredEvents.length;
                    perfettoStats.lastTraceStatsSuccess = true;
                    perfettoStats.lastTraceStatsSize = 1;
                    perfettoStats.traceStatsProducersConnected = 1;
                    perfettoStats.traceStatsProducersSeen = 1;
                    perfettoStats.traceStatsDataSourcesRegistered = 1;
                    perfettoStats.traceStatsDataSourcesSeen = 1;
                    perfettoStats.traceStatsTracingSessions = 1;
                    perfettoStats.traceStatsTotalBuffers = 1;
                    perfettoStats.traceStatsBytesWritten = perfettoStats.lastTraceSize;
                    perfettoStats.trackDescriptorPacketCount = 2;
                    perfettoStats.processDescriptorCount = 1;
                    perfettoStats.threadDescriptorCount = 1;
                    perfettoStats.traceStatsChunksWritten = mirroredEvents.length + perfettoStats.trackDescriptorPacketCount;
                    perfettoStats.tracePacketCount = mirroredEvents.length + perfettoStats.trackDescriptorPacketCount;
                    perfettoStats.trackEventPacketCount = mirroredEvents.length;
                    perfettoStats.trackEventInstantCount = mirroredEvents.filter(function(event) { return event.ph === 'i'; }).length;
                    perfettoStats.trackEventCounterCount = mirroredEvents.filter(function(event) { return event.ph === 'C'; }).length;
                    perfettoStats.trackEventCounterValueCount = perfettoStats.trackEventCounterCount;
                    perfettoStats.trackEventNamedCount = mirroredEvents.length;
                    perfettoStats.trackEventDirectNameCount = mirroredEvents.length;
                    perfettoStats.trackEventInternedNameCount = 0;
                    perfettoStats.trackEventTrackNameCount = mirroredEvents.length;
                    perfettoStats.trackEventNames = trackEventNames;
                    perfettoStats.lastServiceStateSuccess = true;
                    perfettoStats.lastServiceStateSize = 1;
                    perfettoStats.serviceStateProducerCount = 1;
                    perfettoStats.serviceStateDataSourceCount = 1;
                    perfettoStats.serviceStateTracingSessionCount = 0;
                    perfettoStats.serviceStateSupportsTracingSessions = false;
                    perfettoStats.serviceStateNumSessions = 0;
                    perfettoStats.serviceStateNumSessionsStarted = perfettoStats.startedCount;
                    return JSON.stringify({ traceEvents: outputEvents });
                },
                getTraceBufferUsage() {
                    return {
                        value: nativeEvents.length / 100,
                        percentage: nativeEvents.length,
                        eventCount: nativeEvents.length,
                        eventCapacity: 100
                    };
                },
                getPerfettoStats() {
                    return Object.assign({}, perfettoStats);
                },
                getPerfettoTraceData() {
                    return perfettoTraceDataBase64;
                },
                recordInstantEvent(name, data) {
                    assert.strictEqual(nativeRecording, true);
                    nativeEvents.push({
                        cat: 'electron,miniblink',
                        name,
                        ph: 'i',
                        args: {
                            data
                        }
                    });
                    perfettoStats.eventCount = nativeEvents.length;
                },
                recordCounter(name, value) {
                    assert.strictEqual(nativeRecording, true);
                    nativeEvents.push({
                        cat: 'electron,miniblink',
                        name,
                        ph: 'C',
                        args: {
                            value
                        }
                    });
                    perfettoStats.eventCount = nativeEvents.length;
                }
            };
        }
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    delete require.cache[modulePath];
    try {
        const contentTracing = require('../electron/lib/browser/api/content-tracing');

        const categories = await contentTracing.getCategories();
        assert.strictEqual(categories.includes('electron'), true);
        assert.strictEqual(categories.includes('miniblink'), true);
        assert.strictEqual(categories.includes('disabled-by-default-memory-infra'), true);

        await contentTracing.enableHeapProfiling({
            mode: 'browser',
            samplingRate: 2000,
            stackMode: 'native-with-thread-names'
        });

        await contentTracing.startRecording({
            included_categories: ['electron', 'miniblink', 'disabled-by-default-memory-infra'],
            excluded_categories: ['netlog'],
            traceOptions: 'record-continuously'
        });
        assert.ok(nativeStartConfig.categoryFilter.includes('electron'));
        assert.ok(nativeStartConfig.categoryFilter.includes('miniblink'));
        assert.ok(nativeStartConfig.categoryFilter.includes('disabled-by-default-memory-infra'));
        assert.ok(nativeStartConfig.categoryFilter.includes('-netlog'));
        assert.strictEqual(nativeStartConfig.traceOptions, 'record-continuously');
        assert.strictEqual(nativeStartConfig.heapProfiling.enabled, true);
        assert.strictEqual(nativeStartConfig.heapProfiling.mode, 'browser');
        assert.strictEqual(nativeStartConfig.heapProfiling.samplingRate, 2000);
        assert.strictEqual(nativeStartConfig.heapProfiling.stackMode, 'native-with-thread-names');
        assert.strictEqual(nativeStartConfig.heapProfiling.memoryInfraCategoryEnabled, true);

        let duplicateStartRejected = false;
        try {
            await contentTracing.startRecording('*');
        } catch (error) {
            duplicateStartRejected = /already recording/.test(String(error && error.message));
        }
        assert.strictEqual(duplicateStartRejected, true);

        let lateHeapProfilingRejected = false;
        try {
            await contentTracing.enableHeapProfiling();
        } catch (error) {
            lateHeapProfilingRejected = /before startRecording/.test(String(error && error.message));
        }
        assert.strictEqual(lateHeapProfilingRejected, true);

        contentTracing._recordProcessSnapshot('smoke-snapshot', { ok: true });
        const nativeBinding = process._linkedBinding('electron_common_content_tracing');
        nativeBinding.recordCounter('smoke-counter', 7);
        const usage = await contentTracing.getTraceBufferUsage();
        assert.strictEqual(typeof usage.value, 'number');
        assert.strictEqual(typeof usage.percentage, 'number');
        assert.strictEqual(usage.eventCount, 2);

        const outputPath = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'miniblink-content-tracing-')), 'trace.json');
        const writtenPath = await contentTracing.stopRecording(outputPath);
        assert.strictEqual(writtenPath, outputPath);
        const trace = JSON.parse(fs.readFileSync(writtenPath, 'utf8'));
        assert.ok(Array.isArray(trace.traceEvents));
        assert.ok(trace.traceEvents.some(function(event) { return event.name === 'contentTracing.startRecording'; }));
        assert.ok(trace.traceEvents.some(function(event) { return event.name === 'smoke-snapshot'; }));
        assert.ok(trace.traceEvents.some(function(event) {
            return event.name === 'HeapProfiler.session'
                && event.args
                && event.args.samplingRate === 2000
                && event.args.stackMode === 'native-with-thread-names';
        }));
        assert.ok(trace.traceEvents.some(function(event) {
            return event.name === 'smoke-snapshot'
                && event.args
                && event.args.data
                && event.args.data.ok === true;
        }));
        assert.ok(trace.traceEvents.some(function(event) {
            if (event.name !== 'smoke-snapshot' || !event.args || typeof event.args.data !== 'string')
                return false;
            return JSON.parse(event.args.data).data.ok === true;
        }));
        assert.strictEqual(trace.metadata.product, 'miniblink-electron');
        assert.strictEqual(typeof trace.metadata.nodeTraceEventCount, 'number');
        assert.ok(trace.metadata.nodeTraceEventCount > 0);
        assert.strictEqual(typeof trace.metadata.nativeTraceEventCount, 'number');
        assert.ok(trace.metadata.nativeTraceEventCount > 0);
        assert.strictEqual(trace.metadata.nativePerfetto.initialized, true);
        assert.strictEqual(trace.metadata.nativePerfetto.recording, false);
        assert.strictEqual(trace.metadata.nativePerfetto.eventCount, 2);
        assert.ok(trace.metadata.nativePerfetto.lastTraceSize > 0);
        assert.strictEqual(trace.metadata.nativePerfetto.lastTraceStatsSuccess, true);
        assert.ok(trace.metadata.nativePerfetto.lastTraceStatsSize > 0);
        assert.strictEqual(trace.metadata.nativePerfetto.traceStatsProducersConnected, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.traceStatsDataSourcesRegistered, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.traceStatsTracingSessions, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.traceStatsTotalBuffers, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.traceStatsBytesWritten, trace.metadata.nativePerfetto.lastTraceSize);
        assert.strictEqual(trace.metadata.nativePerfetto.traceStatsChunksWritten, 4);
        assert.strictEqual(trace.metadata.nativePerfetto.tracePacketCount, 4);
        assert.strictEqual(trace.metadata.nativePerfetto.trackDescriptorPacketCount, 2);
        assert.strictEqual(trace.metadata.nativePerfetto.processDescriptorCount, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.threadDescriptorCount, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.trackEventPacketCount, 2);
        assert.strictEqual(trace.metadata.nativePerfetto.trackEventInstantCount, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.trackEventCounterCount, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.trackEventCounterValueCount, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.trackEventNamedCount, 2);
        assert.strictEqual(trace.metadata.nativePerfetto.trackEventDirectNameCount, 2);
        assert.strictEqual(trace.metadata.nativePerfetto.trackEventInternedNameCount, 0);
        assert.strictEqual(trace.metadata.nativePerfetto.trackEventTrackNameCount, 2);
        assert.ok(trace.metadata.nativePerfetto.trackEventNames.includes('smoke-snapshot'));
        assert.ok(trace.metadata.nativePerfetto.trackEventNames.includes('smoke-counter'));
        assert.strictEqual(trace.metadata.nativePerfetto.lastServiceStateSuccess, true);
        assert.ok(trace.metadata.nativePerfetto.lastServiceStateSize > 0);
        assert.strictEqual(trace.metadata.nativePerfetto.serviceStateProducerCount, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.serviceStateDataSourceCount, 1);
        assert.strictEqual(trace.metadata.nativePerfetto.serviceStateTracingSessionCount, 0);
        assert.strictEqual(trace.metadata.nativePerfetto.serviceStateSupportsTracingSessions, false);
        assert.strictEqual(trace.metadata.nativePerfetto.serviceStateNumSessions, 0);
        assert.strictEqual(trace.metadata.nativePerfetto.serviceStateNumSessionsStarted, 1);
        assert.strictEqual(typeof trace.metadata.nativePerfetto.traceDataBase64, 'string');
        assert.ok(trace.metadata.nativePerfetto.traceDataBase64.length > 0);
        assert.strictEqual(trace.metadata.traceConfig.nativeTraceEvents.available, true);
        assert.strictEqual(trace.metadata.traceConfig.nativeTraceEvents.enabled, true);
        assert.strictEqual(trace.metadata.traceConfig.heapProfiling.enabled, true);
        assert.strictEqual(trace.metadata.traceConfig.heapProfiling.mode, 'browser');
        assert.strictEqual(trace.metadata.traceConfig.heapProfiling.samplingRate, 2000);
        assert.strictEqual(trace.metadata.traceConfig.heapProfiling.stackMode, 'native-with-thread-names');
        assert.strictEqual(trace.metadata.traceConfig.heapProfiling.memoryInfraCategoryEnabled, true);
        assert.strictEqual(trace.metadata.traceConfig.nativeTraceEvents.heapProfiling.samplingRate, 2000);
        assert.strictEqual(trace.metadata.traceConfig.options.heap_profiling_options.samplingRate, 2000);
        assert.strictEqual(trace.metadata.traceConfig.nodeTraceEvents.available, true);
        assert.strictEqual(trace.metadata.traceConfig.nodeTraceEvents.enabled, true);

        let stopRejected = false;
        try {
            await contentTracing.stopRecording();
        } catch (error) {
            stopRejected = /not recording/.test(String(error && error.message));
        }
        assert.strictEqual(stopRejected, true);

        console.log('PASS content-tracing-js-smoke');
    } finally {
        process._linkedBinding = originalLinkedBinding;
        if (cachedModule)
            require.cache[modulePath] = cachedModule;
        else
            delete require.cache[modulePath];
    }
}

async function runCrashReporterSmoke() {
    const fs = require('fs');
    const http = require('http');
    const os = require('os');
    const path = require('path');
    const modulePath = require.resolve('../electron/lib/browser/api/crash-reporter');
    const originalLinkedBinding = process._linkedBinding;
    function listen(server) {
        return new Promise(function(resolve) {
            server.listen(0, '127.0.0.1', function() {
                resolve(server.address().port);
            });
        });
    }
    function closeServer(server) {
        return new Promise(function(resolve) {
            server.close(resolve);
        });
    }
    function waitForDatabase(predicate) {
        return new Promise(function(resolve, reject) {
            const deadline = Date.now() + 3000;
            function poll() {
                if (predicate()) {
                    resolve();
                    return;
                }
                if (Date.now() > deadline) {
                    reject(new Error('timed out waiting for crash report database'));
                    return;
                }
                setTimeout(poll, 20);
            }
            poll();
        });
    }
    delete require.cache[modulePath];
    process._linkedBinding = function(name) {
        if (name === 'electron_common_crash_reporter') {
            return {
                writeMinidump(filePath) {
                    fs.writeFileSync(filePath, Buffer.from('MDMPsmoke-minidump'));
                    return {
                        path: filePath,
                        size: fs.statSync(filePath).size,
                        streamCount: 5,
                        threadCount: 1,
                        moduleCount: 1
                    };
                },
                installSignalHandlers(directory) {
                    assert.strictEqual(typeof directory, 'string');
                    return {
                        running: true,
                        outOfProcess: true,
                        pid: 4321,
                        portName: 'org.miniblink.test.crash',
                        directory
                    };
                },
                requestCrashServiceDump(filePath) {
                    fs.writeFileSync(filePath, Buffer.from('MDMPsmoke-service-minidump'));
                    return {
                        path: filePath,
                        size: fs.statSync(filePath).size,
                        streamCount: 6,
                        threadCount: 2,
                        moduleCount: 3,
                        servicePid: 4321,
                        outOfProcess: true
                    };
                },
                getCrashServiceStatus() {
                    return {
                        running: true,
                        outOfProcess: true,
                        pid: 4321,
                        portName: 'org.miniblink.test.crash',
                        directory: crashDir
                    };
                },
                stopCrashService() {
                    return true;
                }
            };
        }
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };
    const crashReporter = require('../electron/lib/browser/api/crash-reporter');
    const crashDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miniblink-crash-reporter-'));

    crashReporter.start({
        productName: 'MiniBlink',
        companyName: 'MiniBlink',
        uploadToServer: false,
        crashesDirectory: crashDir,
        globalExtra: {
            channel: 'smoke'
        }
    });

    assert.strictEqual(crashReporter.getUploadToServer(), false);
    assert.strictEqual(crashReporter.getCrashesDirectory(), crashDir);
    assert.strictEqual(crashReporter.getCrashReportFolder(), crashDir);
    assert.deepStrictEqual(crashReporter.getParameters(), { channel: 'smoke' });

    crashReporter.addExtraParameter('build', 132);
    assert.strictEqual(crashReporter.getParameters().build, '132');
    crashReporter.removeExtraParameter('build');
    assert.strictEqual(Object.prototype.hasOwnProperty.call(crashReporter.getParameters(), 'build'), false);

    const report = crashReporter._writeReportForTesting('smoke', new Error('crash smoke'), 'test');
    assert.strictEqual(report.kind, 'smoke');
    assert.strictEqual(report.extra.channel, 'smoke');
    assert.strictEqual(fs.existsSync(report.path), true);
    assert.strictEqual(typeof report.diagnosticReport, 'string');
    assert.strictEqual(fs.existsSync(report.diagnosticReport), true);
    assert.strictEqual(typeof report.minidump, 'string');
    assert.strictEqual(fs.existsSync(report.minidump), true);
    assert.strictEqual(fs.readFileSync(report.minidump).subarray(0, 4).toString('ascii'), 'MDMP');
    assert.strictEqual(report.nativeMinidump.threadCount, 2);
    assert.strictEqual(report.nativeMinidump.moduleCount, 3);
    assert.strictEqual(report.nativeMinidump.servicePid, 4321);
    assert.strictEqual(report.nativeMinidump.outOfProcess, true);
    assert.strictEqual(crashReporter.getLastCrashReport().path, report.path);
    assert.strictEqual(report.database.state, 'completed');
    assert.strictEqual(report.database.path, path.join(crashDir, 'reports'));
    let database = crashReporter._getCrashReportDatabaseForTesting();
    assert.strictEqual(database.path, path.join(crashDir, 'reports'));
    assert.strictEqual(database.new, 0);
    assert.strictEqual(database.completed, 1);
    assert.strictEqual(database.pending, 0);
    assert.strictEqual(database.uploaded, 0);
    assert.strictEqual(fs.existsSync(path.join(crashDir, 'reports', 'completed', report.id + '.json')), true);
    assert.strictEqual(crashReporter._getNativeCrashServiceForTesting().running, true);
    crashReporter.setUploadToServer(true);
    assert.strictEqual(crashReporter.getUploadToServer(), true);
    database = crashReporter._getCrashReportDatabaseForTesting();
    assert.strictEqual(database.pending, 1);
    assert.strictEqual(crashReporter._getPendingReportsForTesting()[0].id, report.id);
    const uploaded = crashReporter._recordUploadCompleteForTesting(report);
    assert.strictEqual(uploaded.id, report.id);
    database = crashReporter._getCrashReportDatabaseForTesting();
    assert.strictEqual(database.pending, 0);
    assert.strictEqual(database.uploaded, 1);
    assert.strictEqual(crashReporter.getUploadedReports()[0].id, report.id);

    const uploadDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miniblink-crash-reporter-upload-'));
    let uploadRequestResolve;
    const uploadRequest = new Promise(function(resolve) {
        uploadRequestResolve = resolve;
    });
    const uploadServer = http.createServer(function(request, response) {
        const chunks = [];
        request.on('data', function(chunk) {
            chunks.push(chunk);
        });
        request.on('end', function() {
            const body = Buffer.concat(chunks);
            response.statusCode = 200;
            response.end('ok');
            uploadRequestResolve({
                headers: request.headers,
                body
            });
        });
    });
    const uploadPort = await listen(uploadServer);
    try {
        crashReporter.start({
            productName: 'MiniBlink',
            companyName: 'MiniBlink',
            uploadToServer: true,
            submitURL: `http://127.0.0.1:${uploadPort}/submit`,
            crashesDirectory: uploadDir,
            globalExtra: {
                channel: 'upload-smoke'
            }
        });
        const uploadedReport = crashReporter._writeReportForTesting('upload', new Error('upload smoke'), 'test');
        assert.strictEqual(uploadedReport.kind, 'upload');
        const receivedUpload = await uploadRequest;
        const contentType = receivedUpload.headers['content-type'];
        assert.ok(/^multipart\/form-data; boundary=/.test(contentType));
        const uploadText = receivedUpload.body.toString('latin1');
        assert.ok(uploadText.includes('name="payload"'));
        assert.ok(uploadText.includes('"kind":"upload"'));
        assert.ok(uploadText.includes('name="channel"'));
        assert.ok(uploadText.includes('upload-smoke'));
        assert.ok(uploadText.includes('name="upload_file_minidump"'));
        assert.ok(receivedUpload.body.includes(Buffer.from('MDMPsmoke-service-minidump')));
        await waitForDatabase(function() {
            const uploadDatabase = crashReporter._getCrashReportDatabaseForTesting();
            return uploadDatabase.pending === 0 && uploadDatabase.uploaded === 1;
        });
    } finally {
        await closeServer(uploadServer);
    }
    process._linkedBinding = originalLinkedBinding;

    const nextDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miniblink-crash-reporter-next-'));
    crashReporter.setCrashReportFolder(nextDir);
    assert.strictEqual(crashReporter.getCrashReportFolder(), nextDir);
    database = crashReporter._getCrashReportDatabaseForTesting();
    assert.strictEqual(database.completed, 0);

    console.log('PASS crash-reporter-js-smoke');
}

function runNetSmoke() {
    const Module = require('module');
    const originalLoad = Module._load;
    const modulePath = require.resolve('../electron/lib/browser/api/net');
    const cachedModule = require.cache[modulePath];
    const app = {
        online: true,
        isOnline() {
            return this.online;
        }
    };
    const calls = [];

    function makeRequest(protocol, options) {
        const headers = Object.create(null);
        return {
            protocol,
            options,
            events: [],
            onceEvents: [],
            writes: [],
            ended: null,
            aborted: false,
            on(event, callback) {
                this.events.push([event, callback]);
                return this;
            },
            once(event, callback) {
                this.onceEvents.push([event, callback]);
                return this;
            },
            setHeader(name, value) {
                headers[name.toLowerCase()] = value;
            },
            getHeader(name) {
                return headers[name.toLowerCase()];
            },
            removeHeader(name) {
                delete headers[name.toLowerCase()];
            },
            write(chunk, encoding, callback) {
                this.writes.push({ chunk, encoding });
                if (callback)
                    callback();
            },
            end(chunk, encoding, callback) {
                this.ended = { chunk, encoding };
                if (callback)
                    callback();
            },
            abort() {
                this.aborted = true;
            },
            followRedirect() {
                this.followed = true;
                return true;
            }
        };
    }

    const httpMock = {
        request(options) {
            calls.push(['http', options]);
            return makeRequest('http', options);
        }
    };
    const httpsMock = {
        request(options) {
            calls.push(['https', options]);
            return makeRequest('https', options);
        }
    };

    delete require.cache[modulePath];
    Module._load = function(request, parent, isMain) {
        if (request === 'electron')
            return { app };
        if (parent && parent.filename === modulePath && request === 'http')
            return httpMock;
        if (parent && parent.filename === modulePath && request === 'https')
            return httpsMock;
        return originalLoad.call(this, request, parent, isMain);
    };

    try {
        const { net } = require('../electron/lib/browser/api/net');

        assert.strictEqual(net.isOnline(), true);
        assert.strictEqual(net.online, true);
        app.online = false;
        assert.strictEqual(net.isOnline(), false);
        assert.strictEqual(net.online, false);

        const secureRequest = net.request('https://example.test/path');
        assert.strictEqual(secureRequest.req.protocol, 'https');
        assert.deepStrictEqual(calls[0], ['https', 'https://example.test/path']);

        const plainRequest = net.request('http://example.test/path');
        assert.strictEqual(plainRequest.req.protocol, 'http');
        assert.deepStrictEqual(calls[1], ['http', 'http://example.test/path']);

        const options = { protocol: 'https:', hostname: 'example.test', session: { id: 1 } };
        const optionRequest = net.request(options);
        assert.strictEqual(optionRequest.req.protocol, 'https');
        assert.strictEqual(options.session, null);

        optionRequest.setHeader('X-Test', '1');
        assert.strictEqual(optionRequest.getHeader('x-test'), '1');
        optionRequest.removeHeader('X-Test');
        assert.strictEqual(optionRequest.getHeader('x-test'), undefined);
        optionRequest.on('response', function() {});
        optionRequest.once('finish', function() {});
        optionRequest.write('chunk', 'utf8');
        optionRequest.end('done', 'utf8');
        assert.deepStrictEqual(optionRequest.req.writes[0], { chunk: 'chunk', encoding: 'utf8' });
        assert.deepStrictEqual(optionRequest.req.ended, { chunk: 'done', encoding: 'utf8' });
        assert.strictEqual(optionRequest.followRedirect(), true);
        optionRequest.abort();
        assert.strictEqual(optionRequest.req.aborted, true);

        console.log('PASS net-js-smoke');
    } finally {
        Module._load = originalLoad;
        if (cachedModule)
            require.cache[modulePath] = cachedModule;
        else
            delete require.cache[modulePath];
    }
}

async function runRendererWrapperSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const ipcRendererPath = require.resolve('../electron/lib/renderer/api/ipc-renderer');
    const contextBridgePath = require.resolve('../electron/lib/renderer/api/context-bridge');
    delete require.cache[ipcRendererPath];
    delete require.cache[contextBridgePath];

    const EventEmitter = require('events').EventEmitter;
    const hiddenIpc = new EventEmitter();
    const ipcBindingCalls = [];
    class FakeIpcRendererBinding {
        send(...args) {
            ipcBindingCalls.push(['send', ...args]);
            return 'sent';
        }

        sendSync(...args) {
            ipcBindingCalls.push(['sendSync', ...args]);
            return 'sync-result';
        }
    }
    class FakeV8Util {
        getHiddenValue(target, key) {
            assert.strictEqual(target, global);
            assert.strictEqual(key, 'ipc');
            return hiddenIpc;
        }
    }
    const contextBridge = {
        exposeInMainWorld(name, value) {
            return { name, value };
        }
    };
    process._linkedBinding = function(name) {
        if (name === 'electron_renderer_ipc')
            return { ipcRenderer: FakeIpcRendererBinding };
        if (name === 'electron_common_v8_util')
            return { v8Util: FakeV8Util };
        if (name === 'electron_renderer_contextbridge')
            return contextBridge;
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    try {
        const ipcRenderer = require('../electron/lib/renderer/api/ipc-renderer');
        assert.strictEqual(ipcRenderer, hiddenIpc);
        assert.strictEqual(ipcRenderer.send('channel', 1), 'sent');
        assert.deepStrictEqual(ipcBindingCalls[0], ['send', 'ipc-message', 'channel', 1]);
        assert.strictEqual(ipcRenderer.sendSync('sync-channel', 2), 'sync-result');
        assert.deepStrictEqual(ipcBindingCalls[1], ['sendSync', 'ipc-message-sync', 'sync-channel', 2]);
        ipcRenderer.sendTo(7, 'target-channel', 'payload');
        ipcRenderer.sendToAll(8, 'all-channel', 'payload');
        assert.deepStrictEqual(ipcBindingCalls[2], ['send', 'ipc-message', 'ELECTRON_BROWSER_SEND_TO', false, 7, 'target-channel', 'payload']);
        assert.deepStrictEqual(ipcBindingCalls[3], ['send', 'ipc-message', 'ELECTRON_BROWSER_SEND_TO', true, 8, 'all-channel', 'payload']);
        assert.throws(function() { ipcRenderer.sendTo('bad', 'channel'); }, /webContentsId/);

        const invokePromise = ipcRenderer.invoke('invoke-channel', 3);
        hiddenIpc.emit('ipc-main-handle-reply-invoke-channel', {}, 'invoke-result');
        assert.strictEqual(await invokePromise, 'invoke-result');
        assert.deepStrictEqual(ipcBindingCalls[4], ['send', 'ipc-render-invoke', 'invoke-channel', 3]);

        assert.strictEqual(require('../electron/lib/renderer/api/context-bridge'), contextBridge);

        console.log('PASS renderer-wrapper-js-smoke');
    } finally {
        process._linkedBinding = originalLinkedBinding;
        delete require.cache[ipcRendererPath];
        delete require.cache[contextBridgePath];
    }
}

function runRendererWebFrameSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const modulePath = require.resolve('../electron/lib/renderer/api/web-frame');
    delete require.cache[modulePath];

    class FakeWebFrame {
        registerEmbedderCustomElement() {}
        setZoomFactor() {}
        getZoomFactor() { return 1; }
        getZoomLevel() { return 0; }
        setZoomLevel() {}
        setZoomLevelLimits() {}
        setVisualZoomLevelLimits() {}
        setLayoutZoomLevelLimits() {}
        registerURLSchemeAsSecure() {}
        registerURLSchemeAsBypassingCSP() {}
        registerURLSchemeAsPrivileged() {}
        executeJavaScript() {}
        removeInsertedCSS() {}
        insertCSS() {}
        insertText(text) {
            this.insertedText = text;
            return true;
        }
        setSpellCheckProvider(language, autoCorrectWord, provider) {
            this.spellCheckProvider = { language, autoCorrectWord, provider };
        }
    }

    process._linkedBinding = function(name) {
        if (name === 'electron_renderer_webframe')
            return { WebFrame: FakeWebFrame };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    try {
        const webFrame = require('../electron/lib/renderer/api/web-frame');
        assert.strictEqual(typeof webFrame.on, 'function');
        assert.strictEqual(typeof webFrame.insertCSS, 'function');
        assert.strictEqual(typeof webFrame.removeInsertedCSS, 'function');
        assert.strictEqual(typeof webFrame.executeJavaScript, 'function');
        assert.strictEqual(typeof webFrame.setZoomFactor, 'function');
        assert.strictEqual(typeof webFrame.getZoomFactor, 'function');
        assert.strictEqual(typeof webFrame.setZoomLevel, 'function');
        assert.strictEqual(typeof webFrame.getZoomLevel, 'function');
        assert.strictEqual(typeof webFrame.setZoomLevelLimits, 'function');
        assert.strictEqual(typeof webFrame.setVisualZoomLevelLimits, 'function');
        assert.strictEqual(typeof webFrame.setLayoutZoomLevelLimits, 'function');
        assert.strictEqual(typeof webFrame.registerURLSchemeAsSecure, 'function');
        assert.strictEqual(typeof webFrame.registerURLSchemeAsBypassingCSP, 'function');
        assert.strictEqual(typeof webFrame.registerURLSchemeAsPrivileged, 'function');
        assert.strictEqual(typeof webFrame.insertText, 'function');
        assert.strictEqual(typeof webFrame.setSpellCheckProvider, 'function');
        assert.strictEqual(webFrame.insertText('typed'), true);
        assert.strictEqual(webFrame.insertedText, 'typed');
        const provider = { spellCheck(words, callback) { callback([]); } };
        webFrame.setSpellCheckProvider('en-US', true, provider);
        assert.deepStrictEqual(webFrame.spellCheckProvider, {
            language: 'en-US',
            autoCorrectWord: true,
            provider
        });
        webFrame.setMaxListeners(0);
        console.log('PASS renderer-webframe-js-smoke');
    } finally {
        process._linkedBinding = originalLinkedBinding;
        delete require.cache[modulePath];
    }
}

async function runRendererRemoteSmoke() {
    const Module = require('module');
    const EventEmitter = require('events').EventEmitter;
    const originalLoad = Module._load;
    const originalLinkedBinding = process._linkedBinding;
    const remotePath = require.resolve('../electron/lib/renderer/api/remote');
    const screenPath = require.resolve('../electron/lib/renderer/api/screen');
    delete require.cache[remotePath];
    delete require.cache[screenPath];

    const hiddenValues = new WeakMap();
    class FakeV8Util {
        getHiddenValue(target, key) {
            const values = hiddenValues.get(target);
            return values && values.get(key);
        }

        setHiddenValue(target, key, value) {
            let values = hiddenValues.get(target);
            if (!values) {
                values = new Map();
                hiddenValues.set(target, values);
            }
            values.set(key, value);
        }

        deleteHiddenValue(target, key) {
            const values = hiddenValues.get(target);
            if (values)
                values.delete(key);
        }
    }

    class FakeCallbacksRegistry {
        constructor() {
            this.nextId = 0;
            this.callbacks = new Map();
        }

        add(callback) {
            const existing = fakeV8Util.getHiddenValue(callback, 'callbackId');
            if (existing)
                return existing;
            const id = ++this.nextId;
            this.callbacks.set(id, callback);
            fakeV8Util.setHiddenValue(callback, 'callbackId', id);
            fakeV8Util.setHiddenValue(callback, 'location', 'remote-smoke');
            return id;
        }

        apply(id, ...args) {
            return this.callbacks.get(id).apply(global, args);
        }

        remove(id) {
            this.callbacks.delete(id);
        }
    }
    FakeCallbacksRegistry.CallbacksRegistry = FakeCallbacksRegistry;

    const fakeV8Util = new FakeV8Util();
    const ipcRenderer = new EventEmitter();
    const syncCalls = [];
    const asyncCalls = [];
    const appMeta = {
        type: 'object',
        id: 101,
        name: 'App',
        members: [
            { name: 'version', enumerable: true, writable: true, type: 'get' },
            { name: 'ping', enumerable: true, writable: false, type: 'method' }
        ],
        proto: null
    };

    ipcRenderer.send = function(channel, ...args) {
        asyncCalls.push([channel, ...args]);
        return true;
    };
    ipcRenderer.sendSync = function(channel, ...args) {
        syncCalls.push([channel, ...args]);
        if (channel === 'ELECTRON_BROWSER_GET_BUILTIN' && args[0] === 'app')
            return appMeta;
        if (channel === 'ELECTRON_BROWSER_REQUIRE')
            return { type: 'value', value: 'required:' + args[0] };
        if (channel === 'ELECTRON_BROWSER_MEMBER_GET')
            return { type: 'value', value: '1.2.3' };
        if (channel === 'ELECTRON_BROWSER_MEMBER_SET')
            return { type: 'value', value: null };
        if (channel === 'ELECTRON_BROWSER_MEMBER_CALL')
            return { type: 'array', value: [{ type: 'value', value: 'pong' }] };
        throw new Error('unexpected remote channel: ' + channel);
    };

    const electronMock = {
        ipcRenderer,
        isPromise(value) {
            return !!value && typeof value.then === 'function';
        },
        CallbacksRegistry: FakeCallbacksRegistry
    };
    const screenMock = { getPrimaryDisplay() { return { id: 1 }; } };
    const browserExportsMock = {};
    Object.defineProperty(browserExportsMock, 'app', {
        enumerable: true,
        get() {
            return {};
        }
    });

    process._linkedBinding = function(name) {
        if (name === 'electron_common_v8_util')
            return { v8Util: FakeV8Util };
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };
    Module._load = function(request, parent, isMain) {
        if (request === 'electron')
            return electronMock;
        if (parent && parent.filename === remotePath && request === './../../browser/api/exports/electron')
            return browserExportsMock;
        if (parent && parent.filename === screenPath && request === '../../common/api/screen')
            return { screen: screenMock };
        return originalLoad.call(this, request, parent, isMain);
    };

    try {
        const remote = require('../electron/lib/renderer/api/remote');
        const app = remote.getBuiltin('app');
        assert.strictEqual(remote.app, app);
        assert.strictEqual(app.version, '1.2.3');
        app.version = { nested: true };
        const memberSetCall = syncCalls.find(call => call[0] === 'ELECTRON_BROWSER_MEMBER_SET');
        assert(memberSetCall);
        assert.strictEqual(memberSetCall[3].type, 'object');
        assert.strictEqual(memberSetCall[3].members[0].name, 'nested');

        const callbackArgs = [];
        const result = app.ping('payload', function(first, second) {
            callbackArgs.push([first, second]);
        });
        assert.deepStrictEqual(result, ['pong']);
        const memberCall = syncCalls.find(call => call[0] === 'ELECTRON_BROWSER_MEMBER_CALL');
        assert(memberCall);
        const wrappedArgs = memberCall[3];
        const callbackMeta = wrappedArgs.find(item => item.type === 'function');
        assert.strictEqual(typeof callbackMeta.id, 'number');
        ipcRenderer.emit('ELECTRON_RENDERER_CALLBACK', {}, callbackMeta.id, {
            type: 'array',
            value: [
                { type: 'value', value: 'first' },
                { type: 'value', value: 'second' }
            ]
        });
        assert.deepStrictEqual(callbackArgs, [['first', 'second']]);

        assert.strictEqual(remote.require('node:path'), 'required:node:path');
        assert.strictEqual(remote.getBuiltin('app'), app);
        assert.strictEqual(require('../electron/lib/renderer/api/screen'), screenMock);
        remote.getBuiltin('app');
        assert.strictEqual(asyncCalls.length, 0);

        console.log('PASS renderer-remote-js-smoke');
    } finally {
        Module._load = originalLoad;
        process._linkedBinding = originalLinkedBinding;
        delete require.cache[remotePath];
        delete require.cache[screenPath];
    }
}

function runUtilityProcessSmoke() {
    const originalLinkedBinding = process._linkedBinding;
    const modulePath = require.resolve('../electron/lib/browser/api/utility-process');
    const messagePortPath = require.resolve('../electron/lib/browser/api/message-port-main');
    const parentPortPath = require.resolve('../electron/lib/browser/api/parent-port');
    const cachedModule = require.cache[modulePath];
    const cachedMessagePort = require.cache[messagePortPath];
    const cachedParentPort = require.cache[parentPortPath];
    const originalParentPort = process.parentPort;
    delete require.cache[modulePath];
    delete require.cache[messagePortPath];
    delete require.cache[parentPortPath];

    const forkCalls = [];
    const parentPortSends = [];
    class FakeHandle {
        constructor() {
            this.pidValue = 4321;
            this.messages = [];
            this.killed = false;
        }

        pid() {
            return this.pidValue;
        }

        postMessage(message, transfer) {
            this.messages.push({ message, transfer });
            return 'posted';
        }

        kill() {
            this.killed = true;
            return true;
        }
    }

    process._linkedBinding = function(name) {
        if (name === 'electron_browser_utility_process') {
            return {
                _fork(payload) {
                    forkCalls.push(payload);
                    return new FakeHandle();
                }
            };
        }
        if (name === 'electron_browser_parent_port') {
            class ParentPort {
                _send(message) {
                    parentPortSends.push(message);
                    return 'parent-posted';
                }
            }
            return { ParentPort };
        }
        if (originalLinkedBinding)
            return originalLinkedBinding.call(process, name);
        throw new Error('unexpected linked binding: ' + name);
    };

    try {
        const MessagePortMain = require('../electron/lib/browser/api/message-port-main');
        const { utilityProcess } = require('../electron/lib/browser/api/utility-process');

        assert.throws(function() { utilityProcess.fork(''); }, /Missing UtilityProcess/);
        assert.throws(function() { utilityProcess.fork('/tmp/worker.js', [1]); }, /args must be/);
        assert.throws(function() { utilityProcess.fork('/tmp/worker.js', [], { execArgv: '--bad' }); }, /execArgv/);
        assert.throws(function() { utilityProcess.fork('/tmp/worker.js', [], { stdio: 'bad' }); }, /stdio/);

        const child = utilityProcess.fork('/tmp/worker.js', ['a'], {
            cwd: '/tmp',
            execArgv: ['--trace-warnings'],
            serviceName: 'mini-service',
            stdio: 'pipe'
        });
        assert.strictEqual(child.pid, 4321);
        assert.strictEqual(typeof child.stdout.on, 'function');
        assert.strictEqual(typeof child.stderr.on, 'function');
        assert.deepStrictEqual(forkCalls[0].modulePath, '/tmp/worker.js');
        assert.deepStrictEqual(forkCalls[0].args, ['a']);
        assert.deepStrictEqual(forkCalls[0].options.stdio, ['ignore', 'pipe', 'pipe']);

        const rawPort = { raw: true };
        const port = new MessagePortMain(rawPort);
        assert.strictEqual(child.postMessage({ hello: true }, [port]), 'posted');
        assert.deepStrictEqual(child._handle.messages[0], { message: { hello: true }, transfer: [rawPort] });

        const events = [];
        child.on('message', value => events.push(['message', value]));
        child.on('exit', () => events.push(['exit']));
        child._handle.emit('message', { data: 1 });
        child._handle.emit('exit');
        assert.deepStrictEqual(events, [['message', { data: 1 }], ['exit']]);
        assert.strictEqual(child.pid, 0);
        assert.strictEqual(child.stdout, null);
        assert.strictEqual(child.stderr, null);

        const killChild = utilityProcess.fork('/tmp/worker.js');
        assert.strictEqual(killChild.kill(), true);
        assert.strictEqual(killChild._handle.killed, true);

        require('../electron/lib/browser/api/parent-port');
        assert.strictEqual(typeof process.parentPort.postMessage, 'function');
        assert.strictEqual(process.parentPort.postMessage({ from: 'child' }), 'parent-posted');
        assert.deepStrictEqual(parentPortSends, [{ from: 'child' }]);
        const parentMessages = [];
        const rawTransferredPort = { raw: true };
        process.parentPort.on('message', event => parentMessages.push(event));
        process.parentPort.emit('message', { data: 'parent', ports: [rawTransferredPort] });
        assert.strictEqual(parentMessages.length, 1);
        assert.strictEqual(parentMessages[0].data, 'parent');
        assert.strictEqual(parentMessages[0].ports.length, 1);
        assert.strictEqual(parentMessages[0].ports[0]._internalPort, rawTransferredPort);

        console.log('PASS utility-process-js-smoke');
    } finally {
        process._linkedBinding = originalLinkedBinding;
        if (originalParentPort === undefined)
            delete process.parentPort;
        else
            process.parentPort = originalParentPort;
        if (cachedModule)
            require.cache[modulePath] = cachedModule;
        else
            delete require.cache[modulePath];
        if (cachedMessagePort)
            require.cache[messagePortPath] = cachedMessagePort;
        else
            delete require.cache[messagePortPath];
        if (cachedParentPort)
            require.cache[parentPortPath] = cachedParentPort;
        else
            delete require.cache[parentPortPath];
    }
}


function runElectronRendererModuleExportsSmoke() {
    const Module = require('module');
    const originalLoad = Module._load;
    const electronMock = {};
    const electronRendererMock = {};
    const NativeImage = function NativeImage() {};
    function FakeCallbacksRegistry() {}
    const remote = { getBuiltin() {} };
    const screen = { getPrimaryDisplay() {} };
    const webFrame = { insertCSS() {} };
    const moduleMocks = {
        '../common/api/clipboard': {},
        '../common/api/callbacks-registry': { CallbacksRegistry: FakeCallbacksRegistry },
        './api/context-bridge': { exposeInMainWorld() {} },
        '../common/api/is-promise': { isPromise(value) { return !!value && typeof value.then === 'function'; } },
        './api/ipc-renderer': { send() {} },
        '../common/api/native-image': { NativeImage },
        './api/remote': remote,
        './api/screen': screen,
        '../common/api/shell': { Shell: {} },
        './api/web-frame': webFrame
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
        assert.strictEqual(electron.CallbacksRegistry, FakeCallbacksRegistry);
        assert.strictEqual(typeof electron.clipboard, 'object');
        assert.strictEqual(typeof electron.contextBridge.exposeInMainWorld, 'function');
        assert.strictEqual(typeof electron.isPromise, 'function');
        assert.strictEqual(typeof electron.ipcRenderer.send, 'function');
        assert.strictEqual(typeof electron.nativeImage, 'function');
        assert.strictEqual(electron.remote, remote);
        assert.strictEqual(electron.screen, screen);
        assert.strictEqual(typeof electron.shell, 'object');
        assert.strictEqual(electron.webFrame, webFrame);
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
    function FakeCallbacksRegistry() {}
    const remote = { getBuiltin() {} };
    const screen = { getPrimaryDisplay() {} };
    const webFrame = { insertCSS() {} };
    const moduleMocks = {
        '../common/api/clipboard': {},
        '../common/api/callbacks-registry': { CallbacksRegistry: FakeCallbacksRegistry },
        './api/context-bridge': { exposeInMainWorld() {} },
        '../common/api/is-promise': { isPromise(value) { return !!value && typeof value.then === 'function'; } },
        './api/ipc-renderer': { send() {} },
        '../common/api/native-image': { NativeImage },
        './api/remote': remote,
        './api/screen': screen,
        '../common/api/shell': { Shell: {} },
        './api/web-frame': webFrame
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
        assert.strictEqual(electronRendererShim.CallbacksRegistry, FakeCallbacksRegistry);
        assert.strictEqual(electronRendererShim.clipboard, electron.clipboard);
        assert.strictEqual(electronRendererShim.contextBridge, electron.contextBridge);
        assert.strictEqual(electronRendererShim.ipcRenderer, electron.ipcRenderer);
        assert.strictEqual(electronRendererShim.nativeImage, NativeImage);
        assert.strictEqual(electronRendererShim.remote, remote);
        assert.strictEqual(electronRendererShim.screen, screen);
        assert.strictEqual(electronRendererShim.shell, electron.shell);
        assert.strictEqual(electronRendererShim.webFrame, webFrame);
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
        return runBrowserWindowWebContentsSmoke();
    })
    .then(function() {
        runSessionApiSmoke();
    })
    .then(function() {
        return runIpcMainApiSmoke();
    })
    .then(function() {
        runMessageChannelMainSmoke();
    })
    .then(function() {
        runWebFrameMainSmoke();
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
        runBrowserViewSmoke();
    })
    .then(function() {
        return runProtocolSmoke();
    })
    .then(function() {
        return runSystemPreferencesSmoke();
    })
    .then(function() {
        return runContentTracingSmoke();
    })
    .then(function() {
        return runCrashReporterSmoke();
    })
    .then(function() {
        runNetSmoke();
    })
    .then(function() {
        return runRendererWrapperSmoke();
    })
    .then(function() {
        runRendererWebFrameSmoke();
    })
    .then(function() {
        return runRendererRemoteSmoke();
    })
    .then(function() {
        runUtilityProcessSmoke();
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
