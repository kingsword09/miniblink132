'use strict';

const EventEmitter = require('events').EventEmitter;
const childProcess = require('child_process');

function loadNativeThemeBinding() {
    if (!process._linkedBinding)
        return null;

    try {
        return process._linkedBinding('electron_browser_native_theme');
    } catch (e) {
        return null;
    }
}

const nativeThemeBinding = loadNativeThemeBinding();

function envFlag(name) {
    const value = process.env[name];
    if (value === '1' || value === 'true' || value === 'dark')
        return true;
    if (value === '0' || value === 'false' || value === 'light')
        return false;
    return null;
}

function systemShouldUseDarkColors() {
    const override = envFlag('MINIBLINK_SYSTEM_DARK_MODE');
    if (override !== null)
        return override;

    if (nativeThemeBinding && typeof nativeThemeBinding.shouldUseDarkColors === 'function')
        return !!nativeThemeBinding.shouldUseDarkColors();

    if (process.platform !== 'darwin')
        return false;

    try {
        const style = childProcess.execFileSync('/usr/bin/defaults', ['read', '-g', 'AppleInterfaceStyle'], {
            encoding: 'utf8',
            stdio: ['ignore', 'pipe', 'ignore']
        });
        return String(style).trim().toLowerCase() === 'dark';
    } catch (e) {
        return false;
    }
}

function watchIntervalMs() {
    const value = Number(process.env.MINIBLINK_NATIVE_THEME_POLL_MS);
    return Number.isFinite(value) && value > 0 ? Math.max(10, value) : 1000;
}

function nativeThemeState() {
    const source = process.env.MINIBLINK_NATIVE_THEME || 'system';
    const highContrast = process.env.MINIBLINK_HIGH_CONTRAST === '1';
    const systemDark = systemShouldUseDarkColors();
    const dark = source === 'dark' || (source === 'system' && systemDark);
    return { source, highContrast, dark };
}

function NativeTheme() {
    this._systemDark = systemShouldUseDarkColors();
    this._watchTimer = null;
    this._nativeWatcher = null;
}

Object.setPrototypeOf(NativeTheme.prototype, EventEmitter.prototype);

NativeTheme.prototype._refreshSystemTheme = function() {
    const nextSystemDark = systemShouldUseDarkColors();
    if (nextSystemDark === this._systemDark)
        return;

    this._systemDark = nextSystemDark;
    if (nativeThemeState().source === 'system')
        this.emit('updated');
}

NativeTheme.prototype._nativeThemeUpdated = function() {
    const nextSystemDark = systemShouldUseDarkColors();
    const changed = nextSystemDark !== this._systemDark;
    this._systemDark = nextSystemDark;
    if (changed && nativeThemeState().source === 'system')
        this.emit('updated');
}

NativeTheme.prototype._startNativeWatcher = function() {
    if (this._nativeWatcher)
        return true;
    if (!nativeThemeBinding || typeof nativeThemeBinding.startWatching !== 'function')
        return false;

    const callback = this._nativeThemeUpdated.bind(this);
    const watcher = nativeThemeBinding.startWatching(callback);
    this._nativeWatcher = watcher || callback;
    return true;
}

NativeTheme.prototype._stopNativeWatcher = function() {
    if (!this._nativeWatcher)
        return;

    if (nativeThemeBinding && typeof nativeThemeBinding.stopWatching === 'function')
        nativeThemeBinding.stopWatching(this._nativeWatcher);
    this._nativeWatcher = null;
}

NativeTheme.prototype._updateWatcher = function() {
    if (this.listenerCount('updated') === 0) {
        this._stopNativeWatcher();
        if (this._watchTimer) {
            clearInterval(this._watchTimer);
            this._watchTimer = null;
        }
        return;
    }

    if (this._startNativeWatcher()) {
        if (this._watchTimer) {
            clearInterval(this._watchTimer);
            this._watchTimer = null;
        }
        return;
    }

    if (!this._watchTimer) {
        this._watchTimer = setInterval(this._refreshSystemTheme.bind(this), watchIntervalMs());
        if (typeof this._watchTimer.unref === 'function')
            this._watchTimer.unref();
    }
}

NativeTheme.prototype.on = function(event, listener) {
    EventEmitter.prototype.on.call(this, event, listener);
    this._updateWatcher();
    return this;
}

NativeTheme.prototype.addListener = NativeTheme.prototype.on;

NativeTheme.prototype.once = function(event, listener) {
    EventEmitter.prototype.once.call(this, event, listener);
    this._updateWatcher();
    return this;
}

NativeTheme.prototype.removeListener = function(event, listener) {
    EventEmitter.prototype.removeListener.call(this, event, listener);
    this._updateWatcher();
    return this;
}

NativeTheme.prototype.off = NativeTheme.prototype.removeListener;

NativeTheme.prototype.removeAllListeners = function(event) {
    EventEmitter.prototype.removeAllListeners.call(this, event);
    this._updateWatcher();
    return this;
}

Object.defineProperties(NativeTheme.prototype, {
    shouldUseDarkColors: { get: function() { return nativeThemeState().dark; } },
    shouldUseHighContrastColors: { get: function() { return nativeThemeState().highContrast; } },
    shouldUseInvertedColorScheme: { get: function() { return false; } },
    themeSource: {
        get: function() { return nativeThemeState().source; },
        set: function(value) {
            process.env.MINIBLINK_NATIVE_THEME = value || 'system';
            this._systemDark = systemShouldUseDarkColors();
            this.emit('updated');
        }
    }
});

module.exports = new NativeTheme();
