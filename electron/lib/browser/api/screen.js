const EventEmitter = require('events').EventEmitter;
const Screen = process._linkedBinding('atom_common_screen').Screen;

Object.setPrototypeOf(Screen.prototype, EventEmitter.prototype)

module.exports = Screen;
