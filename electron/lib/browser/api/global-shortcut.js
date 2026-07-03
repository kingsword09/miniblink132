'use strict';

const registrations = Object.create(null);

function canonicalModifier(part) {
    const key = part.toLowerCase();
    if (key === 'commandorcontrol' || key === 'cmdorctrl')
        return process.platform === 'darwin' ? 'Command' : 'Control';
    if (key === 'command' || key === 'cmd')
        return 'Command';
    if (key === 'control' || key === 'ctrl')
        return 'Control';
    if (key === 'option' || key === 'alt')
        return 'Alt';
    if (key === 'shift')
        return 'Shift';
    if (key === 'super' || key === 'meta')
        return 'Super';
    return '';
}

function canonicalKey(part) {
    if (part.length === 1)
        return part.toUpperCase();

    const key = part.toLowerCase();
    if (key === 'esc')
        return 'Escape';
    if (key === 'return')
        return 'Enter';
    if (key === 'plus')
        return 'Plus';
    if (/^f([1-9]|1[0-9]|2[0-4])$/.test(key))
        return key.toUpperCase();

    return part.charAt(0).toUpperCase() + part.slice(1);
}

function normalizeAccelerator(accelerator) {
    if (typeof accelerator !== 'string')
        return '';

    const parts = accelerator.split('+').map(function(part) {
        return part.trim();
    }).filter(Boolean);
    if (!parts.length)
        return '';

    const modifiers = [];
    let normalKey = '';
    for (let i = 0; i < parts.length; ++i) {
        const modifier = canonicalModifier(parts[i]);
        if (modifier) {
            if (modifiers.indexOf(modifier) !== -1)
                return '';
            modifiers.push(modifier);
            continue;
        }

        if (normalKey)
            return '';
        normalKey = canonicalKey(parts[i]);
    }

    if (!normalKey)
        return '';

    modifiers.sort();
    modifiers.push(normalKey);
    return modifiers.join('+');
}

function nativeGlobalShortcut() {
    if (global.__miniBlinkGlobalShortcutNative)
        return global.__miniBlinkGlobalShortcutNative;

    if (typeof process !== 'undefined' && process._linkedBinding) {
        try {
            return process._linkedBinding('electron_browser_global_shortcut');
        } catch (e) {
        }
    }

    return null;
}

function register(accelerator, callback) {
    const normalized = normalizeAccelerator(accelerator);
    if (!normalized || typeof callback !== 'function' || registrations[normalized])
        return false;

    const nativeApi = nativeGlobalShortcut();
    if (nativeApi && typeof nativeApi.register === 'function' && nativeApi.register(normalized, callback) === false)
        return false;

    registrations[normalized] = callback;
    return true;
}

function isRegistered(accelerator) {
    const normalized = normalizeAccelerator(accelerator);
    return !!(normalized && registrations[normalized]);
}

function unregister(accelerator) {
    const normalized = normalizeAccelerator(accelerator);
    if (!normalized || !registrations[normalized])
        return;

    const nativeApi = nativeGlobalShortcut();
    if (nativeApi && typeof nativeApi.unregister === 'function')
        nativeApi.unregister(normalized);
    delete registrations[normalized];
}

function unregisterAll() {
    const nativeApi = nativeGlobalShortcut();
    if (nativeApi && typeof nativeApi.unregisterAll === 'function')
        nativeApi.unregisterAll();

    Object.keys(registrations).forEach(function(accelerator) {
        delete registrations[accelerator];
    });
}

module.exports = {
    register,
    isRegistered,
    unregister,
    unregisterAll
};
