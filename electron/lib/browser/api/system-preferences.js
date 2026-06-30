'use strict';

const binding = process._linkedBinding('electron_browser_system_preferences');

function normalizeMediaType(mediaType) {
  if (mediaType === 'microphone')
    return 'microphone';
  if (mediaType === 'camera')
    return 'camera';
  throw new TypeError('mediaType must be "microphone" or "camera"');
}

function normalizeUserDefaultType(type) {
  if (type === 'string' || type === 'boolean' || type === 'integer' || type === 'float' || type === 'double')
    return type;
  throw new TypeError('type must be string, boolean, integer, float, or double');
}

const systemPreferences = {
  getAccentColor() {
    return binding.getAccentColor();
  },

  getColor(color) {
    if (typeof color !== 'string' || color.length === 0)
      throw new TypeError('color must be a non-empty string');
    return binding.getColor(color);
  },

  isDarkMode() {
    return binding.isDarkMode();
  },

  isSwipeTrackingFromScrollEventsEnabled() {
    return binding.isSwipeTrackingFromScrollEventsEnabled();
  },

  getUserDefault(name, type) {
    if (typeof name !== 'string' || name.length === 0)
      throw new TypeError('name must be a non-empty string');
    return binding.getUserDefault(name, normalizeUserDefaultType(type));
  },

  setUserDefault(name, type, value) {
    if (typeof name !== 'string' || name.length === 0)
      throw new TypeError('name must be a non-empty string');
    binding.setUserDefault(name, normalizeUserDefaultType(type), value);
  },

  removeUserDefault(name) {
    if (typeof name !== 'string' || name.length === 0)
      throw new TypeError('name must be a non-empty string');
    binding.removeUserDefault(name);
  },

  isTrustedAccessibilityClient(prompt) {
    return binding.isTrustedAccessibilityClient(!!prompt);
  },

  getMediaAccessStatus(mediaType) {
    return binding.getMediaAccessStatus(normalizeMediaType(mediaType));
  },

  askForMediaAccess(mediaType) {
    mediaType = normalizeMediaType(mediaType);
    return new Promise(function(resolve) {
      binding.askForMediaAccess(mediaType, function(granted) {
        resolve(!!granted);
      });
    });
  }
};

module.exports = systemPreferences;
