function PowerSaveBlocker() {
    this._nextId = 1;
    this._blockers = Object.create(null);
    this._native = getNativePowerSaveBlocker();
    this._executionState = ES_CONTINUOUS;
}

var ES_CONTINUOUS = 0x80000000;
var ES_SYSTEM_REQUIRED = 0x00000001;
var ES_DISPLAY_REQUIRED = 0x00000002;

function getNativePowerSaveBlocker() {
    if (global.__miniBlinkPowerSaveBlockerNative)
        return global.__miniBlinkPowerSaveBlockerNative;

    if (typeof process !== "undefined" && process._linkedBinding) {
        try {
            return process._linkedBinding("electron_browser_power_save_blocker");
        } catch (e) {
        }
    }

    return null;
}

PowerSaveBlocker.prototype._updateNativeBlocker = function() {
    var hasAppSuspensionBlocker = false;
    var hasDisplaySleepBlocker = false;
    var blockers = this._blockers;

    Object.keys(blockers).forEach(function(id) {
        var type = blockers[id];
        if (type === "prevent-display-sleep")
            hasDisplaySleepBlocker = true;
        else if (type === "prevent-app-suspension")
            hasAppSuspensionBlocker = true;
    });

    var state = ES_CONTINUOUS;
    if (hasDisplaySleepBlocker)
        state += ES_SYSTEM_REQUIRED + ES_DISPLAY_REQUIRED;
    else if (hasAppSuspensionBlocker)
        state += ES_SYSTEM_REQUIRED;

    if (state === this._executionState)
        return;

    this._executionState = state;
    if (this._native && typeof this._native.setExecutionState === "function")
        this._native.setExecutionState(state);
}

PowerSaveBlocker.prototype.start = function(type) {
    if (type !== "prevent-app-suspension" && type !== "prevent-display-sleep")
        throw new TypeError("Invalid powerSaveBlocker type");

    var id = this._nextId++;
    this._blockers[id] = type;
    this._updateNativeBlocker();
    return id;
}

PowerSaveBlocker.prototype.stop = function(id) {
    if (!this._blockers[id])
        return;

    delete this._blockers[id];
    this._updateNativeBlocker();
}

PowerSaveBlocker.prototype.isStarted = function(id) {
    return !!this._blockers[id];
}

module.exports = new PowerSaveBlocker();
