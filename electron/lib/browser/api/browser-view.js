'use strict';

const binding = process._linkedBinding('electron_browser_browserview');
const BrowserView = binding.BrowserView;

function parseColor(color) {
    if (typeof color === 'number')
        return color >>> 0;
    if (typeof color !== 'string')
        throw new TypeError('color must be a CSS hex string or number');

    let hex = color.trim();
    if (hex[0] === '#')
        hex = hex.slice(1);
    if (hex.length === 3)
        hex = hex.split('').map(ch => ch + ch).join('');
    if (hex.length === 8)
        hex = hex.slice(2);
    if (!/^[0-9a-fA-F]{6}$/.test(hex))
        throw new TypeError('color must be #RGB, #RRGGBB, or #AARRGGBB');

    const red = parseInt(hex.slice(0, 2), 16);
    const green = parseInt(hex.slice(2, 4), 16);
    const blue = parseInt(hex.slice(4, 6), 16);
    return ((blue << 16) | (green << 8) | red) >>> 0;
}

Object.defineProperty(BrowserView.prototype, "webContents", {
    get: function () {
        const webContents = this._getWebContents();
        if (webContents && typeof webContents._init === 'function')
            webContents._init();
        return webContents;
    },
    configurable : true
});

BrowserView.prototype.getBounds = function() {
    return { ...(this._bounds || { x: 0, y: 0, width: 1, height: 1 }) };
};

BrowserView.prototype.setBackgroundColor = function(color) {
    this._backgroundColor = color;
    this._setBackgroundColor(parseColor(color));
};

BrowserView.prototype.getBackgroundColor = function() {
    return this._backgroundColor || null;
};

BrowserView.prototype.setBounds = function(bounds) {
    const normalized = { x: 0, y: 0, width: 1, height: 1, ...(bounds || {}) };
    this._bounds = { ...normalized };
    let x = 0;
    let y = 0;
    let w = 1;
    let h = 1;
    if ("x" in normalized)
        x = normalized.x;
    if ("y" in normalized)
        y = normalized.y;
    if ("width" in normalized)
        w = normalized.width;
    if ("height" in normalized)
        h = normalized.height;
    this._setBounds(x, y, w, h);
};


module.exports = BrowserView;
