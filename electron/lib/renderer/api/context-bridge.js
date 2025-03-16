'use strict'

const contextBridge = process._linkedBinding('atom_renderer_contextbridge');
module.exports = contextBridge;
