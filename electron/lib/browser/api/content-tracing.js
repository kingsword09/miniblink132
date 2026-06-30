'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');

let recording = false;
let traceConfig = null;
let traceEvents = [];
let startHrtime = null;
let nodeTracingHandle = null;
let nodeTraceDirectory = null;
let nodeTraceBaseline = null;
let nodeTraceOwnDirectory = false;
let nativeTracing = null;

const categories = [
  'blink',
  'browser',
  'electron',
  'gpu',
  'input',
  'ipc',
  'javascript',
  'latency',
  'loading',
  'memory',
  'miniblink',
  'net',
  'node',
  'renderer',
  'v8'
];

function readTraceEventsModule() {
  try {
    return require('trace_events');
  } catch (error) {
    return null;
  }
}

function getNativeTracing() {
  if (nativeTracing !== null)
    return nativeTracing;

  nativeTracing = false;
  try {
    if (process && typeof process._linkedBinding === 'function')
      nativeTracing = process._linkedBinding('electron_common_content_tracing');
  } catch (error) {
    nativeTracing = false;
  }
  return nativeTracing || null;
}

function nowMicroseconds() {
  if (!startHrtime)
    return 0;
  return Math.floor(Number(process.hrtime.bigint() - startHrtime) / 1000);
}

function normalizeOptions(options) {
  if (options == null)
    return { categoryFilter: '*', traceOptions: 'record-until-full' };

  if (typeof options === 'string')
    return { categoryFilter: options, traceOptions: 'record-until-full' };

  if (typeof options !== 'object')
    throw new TypeError('contentTracing.startRecording options must be an object or string');

  const categoryFilter = Array.isArray(options.included_categories)
    ? options.included_categories.join(',')
    : (options.categoryFilter || options.category_filter || '*');
  const traceOptions = options.traceOptions || options.trace_options || options.recordMode || 'record-until-full';

  return {
    categoryFilter: String(categoryFilter || '*'),
    traceOptions: String(traceOptions || 'record-until-full'),
    options: Object.assign({}, options)
  };
}

function addEvent(name, args) {
  if (!recording)
    return;

  traceEvents.push({
    cat: 'electron,miniblink',
    name,
    ph: 'i',
    s: 'p',
    pid: process.pid,
    tid: 0,
    ts: nowMicroseconds(),
    args: args || {}
  });
}

function addSnapshot(name, data) {
  addEvent(name, {
    memoryUsage: process.memoryUsage ? process.memoryUsage() : null,
    resourceUsage: process.resourceUsage ? process.resourceUsage() : null,
    data: data || null
  });
}

function parseCategoryFilter(filter) {
  const value = String(filter || '*').trim();
  const backendCategories = ['node', 'v8'];
  if (!value || value === '*')
    return backendCategories;

  const requestedCategories = value.split(',')
    .map(function(category) { return category.trim(); })
    .filter(function(category) {
      return category && category[0] !== '-';
    })
    .map(function(category) {
      return category[0] === '+' ? category.slice(1) : category;
    });

  backendCategories.forEach(function(category) {
    if (requestedCategories.indexOf(category) < 0)
      requestedCategories.push(category);
  });
  return requestedCategories;
}

function listNodeTraceFileState(directory) {
  const result = Object.create(null);
  let entries = [];
  try {
    entries = fs.readdirSync(directory);
  } catch (error) {
    return result;
  }

  entries.forEach(function(entry) {
    if (!/^node_trace\.\d+\.log$/.test(entry))
      return;

    const filePath = path.join(directory, entry);
    try {
      const stat = fs.statSync(filePath);
      if (!stat.isFile())
        return;
      result[filePath] = {
        size: stat.size,
        mtimeMs: stat.mtimeMs
      };
    } catch (error) {
    }
  });
  return result;
}

function startNodeTracing(config) {
  const traceEvents = readTraceEventsModule();
  if (!traceEvents || typeof traceEvents.createTracing !== 'function') {
    config.nodeTraceEvents = {
      available: false,
      reason: 'trace_events module is unavailable'
    };
    return;
  }

  const nodeCategories = parseCategoryFilter(config.categoryFilter);
  if (nodeCategories.length === 0) {
    config.nodeTraceEvents = {
      available: true,
      enabled: false,
      reason: 'no positive Node/V8 categories were requested'
    };
    return;
  }

  try {
    nodeTraceDirectory = process.cwd();
    nodeTraceOwnDirectory = false;
    nodeTraceBaseline = listNodeTraceFileState(nodeTraceDirectory);
    nodeTracingHandle = traceEvents.createTracing({ categories: nodeCategories });
    nodeTracingHandle.enable();
    config.nodeTraceEvents = {
      available: true,
      enabled: true,
      categories: nodeCategories.slice(),
      intermediateDirectory: nodeTraceDirectory
    };
  } catch (error) {
    nodeTracingHandle = null;
    nodeTraceDirectory = null;
    nodeTraceBaseline = null;
    nodeTraceOwnDirectory = false;
    config.nodeTraceEvents = {
      available: false,
      reason: String(error && error.message ? error.message : error)
    };
  }
}

function stopNodeTracing() {
  if (!nodeTracingHandle)
    return;

  try {
    nodeTracingHandle.disable();
  } catch (error) {
  }
}

function readNewNodeTraceEvents() {
  if (!nodeTraceDirectory || !nodeTraceBaseline)
    return [];

  const current = listNodeTraceFileState(nodeTraceDirectory);
  const events = [];
  Object.keys(current).forEach(function(filePath) {
    const before = nodeTraceBaseline[filePath];
    const after = current[filePath];
    if (before && before.size === after.size && before.mtimeMs === after.mtimeMs)
      return;

    let content = '';
    try {
      content = fs.readFileSync(filePath, 'utf8');
    } catch (error) {
      return;
    }

    try {
      parseNodeTraceEvents(content).forEach(function(event) {
        events.push(event);
      });
    } catch (error) {
    }
  });
  return events;
}

function parseNodeTraceEvents(content) {
  try {
    const parsed = JSON.parse(content);
    return Array.isArray(parsed.traceEvents) ? parsed.traceEvents : [];
  } catch (error) {
  }

  const marker = '"traceEvents":[';
  const markerIndex = content.indexOf(marker);
  if (markerIndex < 0)
    return [];

  const events = [];
  const bodyStart = markerIndex + marker.length;
  let objectStart = -1;
  let depth = 0;
  let inString = false;
  let escaped = false;

  for (let i = bodyStart; i < content.length; ++i) {
    const ch = content[i];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (ch === '\\') {
        escaped = true;
      } else if (ch === '"') {
        inString = false;
      }
      continue;
    }

    if (ch === '"') {
      inString = true;
      continue;
    }

    if (ch === '{') {
      if (depth === 0)
        objectStart = i;
      ++depth;
      continue;
    }

    if (ch !== '}')
      continue;

    if (depth === 0)
      continue;
    --depth;
    if (depth === 0 && objectStart >= 0) {
      try {
        const event = JSON.parse(content.slice(objectStart, i + 1));
        if (event && typeof event === 'object')
          events.push(event);
      } catch (error) {
      }
      objectStart = -1;
    }
  }

  return events;
}

function cleanupNodeTraceDirectory() {
  if (!nodeTraceDirectory)
    return;

  try {
    const baseline = nodeTraceBaseline || Object.create(null);
    fs.readdirSync(nodeTraceDirectory).forEach(function(entry) {
      if (!/^node_trace\.\d+\.log$/.test(entry))
        return;

      const filePath = path.join(nodeTraceDirectory, entry);
      if (baseline[filePath])
        return;

      try {
        fs.unlinkSync(filePath);
      } catch (error) {
      }
    });
    if (nodeTraceOwnDirectory)
      fs.rmdirSync(nodeTraceDirectory);
  } catch (error) {
  }
}

function resetNodeTracing() {
  cleanupNodeTraceDirectory();
  nodeTracingHandle = null;
  nodeTraceDirectory = null;
  nodeTraceBaseline = null;
  nodeTraceOwnDirectory = false;
}

function startNativeTracing(config) {
  const binding = getNativeTracing();
  if (!binding || typeof binding.startRecording !== 'function') {
    config.nativeTraceEvents = {
      available: false,
      reason: 'electron_common_content_tracing binding is unavailable'
    };
    return;
  }

  try {
    binding.startRecording(config.categoryFilter, config.traceOptions);
    config.nativeTraceEvents = {
      available: true,
      enabled: true,
      categoryFilter: config.categoryFilter,
      traceOptions: config.traceOptions
    };
  } catch (error) {
    config.nativeTraceEvents = {
      available: false,
      reason: String(error && error.message ? error.message : error)
    };
  }
}

function parseNativeTraceEvents(content) {
  if (!content)
    return [];
  try {
    const parsed = typeof content === 'string' ? JSON.parse(content) : content;
    return Array.isArray(parsed.traceEvents) ? parsed.traceEvents : [];
  } catch (error) {
    return [];
  }
}

function stopNativeTracing() {
  const binding = getNativeTracing();
  if (!binding || typeof binding.stopRecording !== 'function')
    return [];

  try {
    return parseNativeTraceEvents(binding.stopRecording());
  } catch (error) {
    return [];
  }
}

function waitForNodeTraceFlush() {
  return new Promise(function(resolve) {
    setTimeout(resolve, 100);
  });
}

function traceOutputPath(requestedPath) {
  if (requestedPath) {
    const output = path.resolve(String(requestedPath));
    fs.mkdirSync(path.dirname(output), { recursive: true });
    return output;
  }

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'miniblink-trace-'));
  return path.join(dir, `trace-${process.pid}-${Date.now()}.json`);
}

function writeTraceFile(filePath) {
  const nodeEvents = readNewNodeTraceEvents();
  const nativeEvents = stopNativeTracing();
  const payload = {
    traceEvents: traceEvents.concat(nativeEvents, nodeEvents),
    metadata: {
      product: 'miniblink-electron',
      pid: process.pid,
      platform: process.platform,
      arch: process.arch,
      startTime: traceConfig ? traceConfig.startedAt : null,
      endTime: new Date().toISOString(),
      traceConfig,
      nodeTraceEventCount: nodeEvents.length,
      nativeTraceEventCount: nativeEvents.length
    }
  };
  fs.writeFileSync(filePath, JSON.stringify(payload, null, 2));
  return filePath;
}

const contentTracing = {
  getCategories() {
    return Promise.resolve(categories.slice());
  },

  startRecording(options) {
    if (recording)
      return Promise.reject(new Error('contentTracing is already recording'));

    traceConfig = normalizeOptions(options);
    traceConfig.startedAt = new Date().toISOString();
    startHrtime = process.hrtime.bigint();
    traceEvents = [];
    recording = true;
    startNativeTracing(traceConfig);
    startNodeTracing(traceConfig);
    addSnapshot('contentTracing.startRecording', traceConfig);
    return Promise.resolve();
  },

  async stopRecording(resultFilePath) {
    if (!recording)
      return Promise.reject(new Error('contentTracing is not recording'));

    addSnapshot('contentTracing.stopRecording');
    stopNodeTracing();
    await waitForNodeTraceFlush();
    recording = false;
    const output = traceOutputPath(resultFilePath);
    const writtenPath = writeTraceFile(output);
    traceConfig = null;
    startHrtime = null;
    resetNodeTracing();
    return Promise.resolve(writtenPath);
  },

  getTraceBufferUsage() {
    const binding = getNativeTracing();
    if (binding && typeof binding.getTraceBufferUsage === 'function') {
      try {
        const usage = binding.getTraceBufferUsage();
        if (usage && typeof usage.value === 'number' && typeof usage.percentage === 'number')
          return Promise.resolve(usage);
      } catch (error) {
      }
    }

    const eventCount = traceEvents.length;
    const capacity = 10000;
    return Promise.resolve({
      value: Math.min(1, eventCount / capacity),
      percentage: Math.min(100, (eventCount / capacity) * 100)
    });
  },

  _recordProcessSnapshot(name, data) {
    addSnapshot(name || 'contentTracing.snapshot', data);
    const binding = getNativeTracing();
    if (binding && typeof binding.recordInstantEvent === 'function') {
      try {
        binding.recordInstantEvent(name || 'contentTracing.snapshot');
      } catch (error) {
      }
    }
  }
};

module.exports = contentTracing;
