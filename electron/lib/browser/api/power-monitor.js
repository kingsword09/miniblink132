const EventEmitter = require('events').EventEmitter;
const binding = process._linkedBinding('electron_browser_powermonitor');
const ApiPowerMonitor = binding.ApiPowerMonitor;

Object.setPrototypeOf(ApiPowerMonitor.prototype, EventEmitter.prototype);

module.exports = new ApiPowerMonitor();
