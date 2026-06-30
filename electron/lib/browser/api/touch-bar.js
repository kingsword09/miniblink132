'use strict';

let nextId = 1;

function makeId(prefix) {
  return `${prefix}-${nextId++}`;
}

function assertOptions(options, name) {
  if (options == null)
    return {};
  if (typeof options !== 'object')
    throw new TypeError(`${name} options must be an object`);
  return options;
}

function normalizeColor(color) {
  if (color == null || color === '')
    return null;
  if (typeof color !== 'string')
    throw new TypeError('TouchBar color must be a string');
  const value = color.charAt(0) === '#' ? color : `#${color}`;
  if (!/^#[0-9a-f]{6}$/i.test(value))
    throw new TypeError('TouchBar color must be a 6-digit hex color');
  return value.toUpperCase();
}

function normalizeImage(image) {
  if (image == null || image === '')
    return null;

  if (typeof image === 'string')
    return image;

  if (typeof Buffer !== 'undefined' && Buffer.isBuffer(image))
    return `data:image/png;base64,${image.toString('base64')}`;

  if (image && typeof image.toDataURL === 'function') {
    const dataURL = image.toDataURL();
    return dataURL ? String(dataURL) : null;
  }

  if (image && typeof image.toPNG === 'function') {
    const png = image.toPNG();
    if (typeof Buffer !== 'undefined' && Buffer.isBuffer(png))
      return `data:image/png;base64,${png.toString('base64')}`;
  }

  return null;
}

function normalizeScrubberItem(item) {
  if (typeof item === 'string')
    return { label: item, image: null };
  return {
    label: String(item && item.label ? item.label : ''),
    image: normalizeImage(item && item.image)
  };
}

class TouchBarItem {
  constructor(type) {
    this.id = makeId(type);
    this.type = type;
    this.enabled = true;
  }

  _serialize() {
    return {
      id: this.id,
      type: this.type,
      enabled: this.enabled !== false
    };
  }

  _handleAction() {
    return false;
  }
}

class TouchBarButton extends TouchBarItem {
  constructor(options) {
    options = assertOptions(options, 'TouchBarButton');
    super('button');
    this.label = options.label || '';
    this.image = options.image || null;
    this.backgroundColor = normalizeColor(options.backgroundColor);
    this.enabled = options.enabled !== false;
    this.click = typeof options.click === 'function' ? options.click : null;
  }

  _serialize() {
    const data = super._serialize();
    data.label = String(this.label || '');
    data.image = normalizeImage(this.image);
    data.backgroundColor = this.backgroundColor;
    return data;
  }

  _handleAction(action) {
    if (action.type === 'click' && this.click)
      this.click();
  }
}

class TouchBarLabel extends TouchBarItem {
  constructor(options) {
    options = assertOptions(options, 'TouchBarLabel');
    super('label');
    this.label = options.label || '';
    this.textColor = normalizeColor(options.textColor);
  }

  _serialize() {
    const data = super._serialize();
    data.label = String(this.label || '');
    data.textColor = this.textColor;
    return data;
  }
}

class TouchBarSlider extends TouchBarItem {
  constructor(options) {
    options = assertOptions(options, 'TouchBarSlider');
    super('slider');
    this.label = options.label || '';
    this.minValue = Number.isFinite(Number(options.minValue)) ? Number(options.minValue) : 0;
    this.maxValue = Number.isFinite(Number(options.maxValue)) ? Number(options.maxValue) : 100;
    this.value = Number.isFinite(Number(options.value)) ? Number(options.value) : this.minValue;
    this.change = typeof options.change === 'function' ? options.change : null;
  }

  _serialize() {
    const data = super._serialize();
    data.label = String(this.label || '');
    data.minValue = this.minValue;
    data.maxValue = this.maxValue;
    data.value = this.value;
    return data;
  }

  _handleAction(action) {
    if (action.type !== 'change')
      return;
    this.value = Number(action.value);
    if (this.change)
      this.change(this.value);
  }
}

class TouchBarSegmentedControl extends TouchBarItem {
  constructor(options) {
    options = assertOptions(options, 'TouchBarSegmentedControl');
    super('segmented-control');
    this.segmentStyle = options.segmentStyle || 'automatic';
    this.mode = options.mode || 'single';
    this.segments = Array.isArray(options.segments) ? options.segments.slice() : [];
    this.selectedIndex = Number.isInteger(options.selectedIndex) ? options.selectedIndex : -1;
    this.change = typeof options.change === 'function' ? options.change : null;
  }

  _serialize() {
    const data = super._serialize();
    data.segmentStyle = String(this.segmentStyle || 'automatic');
    data.mode = String(this.mode || 'single');
    data.selectedIndex = this.selectedIndex;
    data.segments = this.segments.map(function(segment) {
      if (typeof segment === 'string')
        return { label: segment, image: null, enabled: true };
      return {
        label: String(segment && segment.label ? segment.label : ''),
        image: normalizeImage(segment && segment.image),
        enabled: !segment || segment.enabled !== false
      };
    });
    return data;
  }

  _handleAction(action) {
    if (action.type !== 'change')
      return;
    this.selectedIndex = Number(action.selectedIndex);
    if (this.change)
      this.change(this.selectedIndex, action.isSelected !== false);
  }
}

class TouchBarColorPicker extends TouchBarItem {
  constructor(options) {
    options = assertOptions(options, 'TouchBarColorPicker');
    super('color-picker');
    this.availableColors = Array.isArray(options.availableColors) ? options.availableColors.map(normalizeColor) : [];
    this.selectedColor = normalizeColor(options.selectedColor || options.color || '#FFFFFF');
    this.change = typeof options.change === 'function' ? options.change : null;
  }

  _serialize() {
    const data = super._serialize();
    data.availableColors = this.availableColors;
    data.selectedColor = this.selectedColor;
    return data;
  }

  _handleAction(action) {
    if (action.type !== 'change')
      return;
    this.selectedColor = normalizeColor(action.color);
    if (this.change)
      this.change(this.selectedColor);
  }
}

class TouchBarScrubber extends TouchBarItem {
  constructor(options) {
    options = assertOptions(options, 'TouchBarScrubber');
    super('scrubber');
    this.items = Array.isArray(options.items) ? options.items.slice() : [];
    this.selectedStyle = options.selectedStyle || null;
    this.overlayStyle = options.overlayStyle || null;
    this.showArrowButtons = !!options.showArrowButtons;
    this.mode = options.mode || 'free';
    this.continuous = !!options.continuous;
    this.selectedIndex = Number.isInteger(options.selectedIndex) ? options.selectedIndex : -1;
    this.select = typeof options.select === 'function' ? options.select : null;
    this.highlight = typeof options.highlight === 'function' ? options.highlight : null;
  }

  _serialize() {
    const data = super._serialize();
    data.items = this.items.map(normalizeScrubberItem);
    data.selectedStyle = this.selectedStyle;
    data.overlayStyle = this.overlayStyle;
    data.showArrowButtons = this.showArrowButtons;
    data.mode = this.mode;
    data.continuous = this.continuous;
    data.selectedIndex = this.selectedIndex;
    return data;
  }

  _handleAction(action) {
    if (action.type === 'select') {
      this.selectedIndex = Number(action.selectedIndex);
      if (this.select)
      this.select(Number(action.selectedIndex));
    }
    if (action.type === 'highlight' && this.highlight)
      this.highlight(Number(action.highlightedIndex));
  }
}

class TouchBarSpacer extends TouchBarItem {
  constructor(options) {
    options = assertOptions(options, 'TouchBarSpacer');
    super('spacer');
    this.size = options.size || 'small';
  }

  _serialize() {
    const data = super._serialize();
    data.size = String(this.size || 'small');
    return data;
  }
}

class TouchBarGroup extends TouchBarItem {
  constructor(options) {
    options = assertOptions(options, 'TouchBarGroup');
    super('group');
    this.label = options.label || '';
    this.items = normalizeItems(options.items || []);
  }

  _serialize() {
    const data = super._serialize();
    data.label = String(this.label || '');
    data.items = this.items.map(function(item) { return item._serialize(); });
    return data;
  }

  _handleAction(action) {
    return dispatchToItems(this.items, action);
  }
}

class TouchBarPopover extends TouchBarItem {
  constructor(options) {
    options = assertOptions(options, 'TouchBarPopover');
    super('popover');
    this.label = options.label || '';
    this.icon = options.icon || options.image || null;
    this.items = normalizeItems(options.items || []);
    this.showCloseButton = options.showCloseButton !== false;
  }

  _serialize() {
    const data = super._serialize();
    data.label = String(this.label || '');
    data.image = normalizeImage(this.icon);
    data.showCloseButton = this.showCloseButton;
    data.items = this.items.map(function(item) { return item._serialize(); });
    return data;
  }

  _handleAction(action) {
    return dispatchToItems(this.items, action);
  }
}

function normalizeItems(items) {
  if (!Array.isArray(items))
    throw new TypeError('TouchBar items must be an array');
  return items.map(function(item) {
    if (item instanceof TouchBarItem)
      return item;
    if (item && Array.isArray(item.items))
      return new TouchBar({ items: item.items });
    throw new TypeError('TouchBar item must be a TouchBar item instance');
  });
}

function dispatchToItems(items, action) {
  for (const item of items) {
    if (item.id === action.id) {
      item._handleAction(action);
      return true;
    }
    if ((item instanceof TouchBarGroup || item instanceof TouchBarPopover) && item._handleAction(action))
      return true;
  }
  return false;
}

class TouchBar {
  constructor(options) {
    options = assertOptions(options, 'TouchBar');
    this.id = makeId('touchbar');
    this.items = normalizeItems(options.items || []);
  }

  _serialize() {
    return {
      id: this.id,
      items: this.items.map(function(item) { return item._serialize(); })
    };
  }

  _handleAction(action) {
    return dispatchToItems(this.items, action);
  }

  _handleActionFromNative(json) {
    if (!json)
      return false;
    const action = typeof json === 'string' ? JSON.parse(json) : json;
    return this._handleAction(action);
  }
}

TouchBar.TouchBarButton = TouchBarButton;
TouchBar.TouchBarColorPicker = TouchBarColorPicker;
TouchBar.TouchBarGroup = TouchBarGroup;
TouchBar.TouchBarLabel = TouchBarLabel;
TouchBar.TouchBarPopover = TouchBarPopover;
TouchBar.TouchBarScrubber = TouchBarScrubber;
TouchBar.TouchBarSegmentedControl = TouchBarSegmentedControl;
TouchBar.TouchBarSlider = TouchBarSlider;
TouchBar.TouchBarSpacer = TouchBarSpacer;

module.exports = TouchBar;
