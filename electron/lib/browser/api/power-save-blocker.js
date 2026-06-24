function PowerSaveBlocker() {
    this._nextId = 1;
    this._blockers = Object.create(null);
}

PowerSaveBlocker.prototype.start = function(type) {
    if (type !== "prevent-app-suspension" && type !== "prevent-display-sleep")
        throw new TypeError("Invalid powerSaveBlocker type");

    var id = this._nextId++;
    this._blockers[id] = type;
    return id;
}

PowerSaveBlocker.prototype.stop = function(id) {
    delete this._blockers[id];
}

PowerSaveBlocker.prototype.isStarted = function(id) {
    return !!this._blockers[id];
}

module.exports = new PowerSaveBlocker();
