'use strict';

const EventEmitter = require('events').EventEmitter;

function nativeThemeState() {
    const source = process.env.MINIBLINK_NATIVE_THEME || 'system';
    const highContrast = process.env.MINIBLINK_HIGH_CONTRAST === '1';
    const dark = source === 'dark';
    return { source, highContrast, dark };
}

function NativeTheme() {}

Object.setPrototypeOf(NativeTheme.prototype, EventEmitter.prototype);

Object.defineProperties(NativeTheme.prototype, {
    shouldUseDarkColors: { get: function() { return nativeThemeState().dark; } },
    shouldUseHighContrastColors: { get: function() { return nativeThemeState().highContrast; } },
    shouldUseInvertedColorScheme: { get: function() { return false; } },
    themeSource: {
        get: function() { return nativeThemeState().source; },
        set: function(value) {
            process.env.MINIBLINK_NATIVE_THEME = value || 'system';
            this.emit('updated');
        }
    }
});

module.exports = new NativeTheme();
