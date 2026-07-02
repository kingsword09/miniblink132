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
let heapProfilingOptions = null;

const heapProfilingModes = [
  'all',
  'browser',
  'gpu',
  'minimal',
  'renderer-sampling',
  'all-renderers',
  'utility-sampling',
  'all-utilities',
  'utility-and-browser'
];

const heapProfilingStackModes = [
  'native',
  'native-with-thread-names'
];

const categories = [
  'blink',
  'browser',
  'disabled-by-default-memory-infra',
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

  let categoryFilter = options.categoryFilter || options.category_filter;
  if (Array.isArray(options.included_categories) || Array.isArray(options.excluded_categories)) {
    const categoryParts = [];
    if (Array.isArray(options.included_categories)) {
      options.included_categories.forEach(function(category) {
        if (category != null && String(category).trim())
          categoryParts.push(String(category).trim());
      });
    }
    if (Array.isArray(options.excluded_categories)) {
      options.excluded_categories.forEach(function(category) {
        if (category != null && String(category).trim())
          categoryParts.push('-' + String(category).replace(/^-+/, '').trim());
      });
    }
    categoryFilter = categoryParts.length ? categoryParts.join(',') : categoryFilter;
  }
  categoryFilter = categoryFilter || '*';
  const traceOptions = options.traceOptions || options.trace_options || options.recordMode || 'record-until-full';

  return {
    categoryFilter: String(categoryFilter || '*'),
    traceOptions: String(traceOptions || 'record-until-full'),
    options: Object.assign({}, options)
  };
}

function normalizeHeapProfilingOptions(options) {
  if (options == null)
    options = {};
  if (typeof options !== 'object')
    throw new TypeError('contentTracing.enableHeapProfiling options must be an object');

  const mode = options.mode == null ? 'all' : String(options.mode);
  if (heapProfilingModes.indexOf(mode) < 0)
    throw new TypeError('contentTracing.enableHeapProfiling mode is invalid');

  const samplingRate = options.samplingRate == null ? 100000 : Number(options.samplingRate);
  if (!Number.isInteger(samplingRate) || samplingRate < 1000 || samplingRate > 10000000)
    throw new TypeError('contentTracing.enableHeapProfiling samplingRate must be an integer between 1000 and 10000000');

  const stackMode = options.stackMode == null ? 'native' : String(options.stackMode);
  if (heapProfilingStackModes.indexOf(stackMode) < 0)
    throw new TypeError('contentTracing.enableHeapProfiling stackMode is invalid');

  return {
    enabled: true,
    mode,
    samplingRate,
    stackMode
  };
}

function isMemoryInfraCategoryEnabled(categoryFilter) {
  const value = String(categoryFilter || '');
  if (!value)
    return false;

  return value.split(',').some(function(category) {
    const normalized = category.trim();
    return normalized === 'disabled-by-default-memory-infra'
      || normalized === 'memory-infra';
  });
}

function applyHeapProfilingOptions(config) {
  if (!heapProfilingOptions)
    return;

  config.heapProfiling = Object.assign({}, heapProfilingOptions, {
    memoryInfraCategoryEnabled: isMemoryInfraCategoryEnabled(config.categoryFilter)
  });

  config.options = Object.assign({}, config.options || {}, {
    heap_profiling_options: Object.assign({}, heapProfilingOptions)
  });
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
  const payload = {
    memoryUsage: process.memoryUsage ? process.memoryUsage() : null,
    resourceUsage: process.resourceUsage ? process.resourceUsage() : null,
    data: data || null
  };
  addEvent(name, payload);
  return payload;
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
    binding.startRecording(config.categoryFilter, config.traceOptions, config.heapProfiling || null);
    config.nativeTraceEvents = {
      available: true,
      enabled: true,
      categoryFilter: config.categoryFilter,
      traceOptions: config.traceOptions,
      heapProfiling: config.heapProfiling || null
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

function getNativePerfettoStats() {
  const binding = getNativeTracing();
  if (!binding || typeof binding.getPerfettoStats !== 'function')
    return null;

  try {
    const stats = binding.getPerfettoStats();
    if (!stats || typeof stats !== 'object')
      return null;
    return {
      initialized: !!stats.initialized,
      recording: !!stats.recording,
      startedCount: Number(stats.startedCount || 0),
      eventCount: Number(stats.eventCount || 0),
      droppedEventCount: Number(stats.droppedEventCount || 0),
      lastTraceSize: Number(stats.lastTraceSize || 0),
      lastTracePacketCountHint: Number(stats.lastTracePacketCountHint || 0),
      lastTraceStatsSuccess: !!stats.lastTraceStatsSuccess,
      lastTraceStatsSize: Number(stats.lastTraceStatsSize || 0),
      traceStatsProducersConnected: Number(stats.traceStatsProducersConnected || 0),
      traceStatsProducersSeen: Number(stats.traceStatsProducersSeen || 0),
      traceStatsDataSourcesRegistered: Number(stats.traceStatsDataSourcesRegistered || 0),
      traceStatsDataSourcesSeen: Number(stats.traceStatsDataSourcesSeen || 0),
      traceStatsTracingSessions: Number(stats.traceStatsTracingSessions || 0),
      traceStatsTotalBuffers: Number(stats.traceStatsTotalBuffers || 0),
      traceStatsBytesWritten: Number(stats.traceStatsBytesWritten || 0),
      traceStatsChunksWritten: Number(stats.traceStatsChunksWritten || 0),
      tracePacketCount: Number(stats.tracePacketCount || 0),
      trackDescriptorPacketCount: Number(stats.trackDescriptorPacketCount || 0),
      processDescriptorCount: Number(stats.processDescriptorCount || 0),
      threadDescriptorCount: Number(stats.threadDescriptorCount || 0),
      trackEventPacketCount: Number(stats.trackEventPacketCount || 0),
      trackEventSliceBeginCount: Number(stats.trackEventSliceBeginCount || 0),
      trackEventSliceEndCount: Number(stats.trackEventSliceEndCount || 0),
      trackEventInstantCount: Number(stats.trackEventInstantCount || 0),
      trackEventCounterCount: Number(stats.trackEventCounterCount || 0),
      trackEventCounterValueCount: Number(stats.trackEventCounterValueCount || 0),
      trackEventNamedCount: Number(stats.trackEventNamedCount || 0),
      trackEventDirectNameCount: Number(stats.trackEventDirectNameCount || 0),
      trackEventInternedNameCount: Number(stats.trackEventInternedNameCount || 0),
      trackEventTrackNameCount: Number(stats.trackEventTrackNameCount || 0),
      trackEventNames: typeof stats.trackEventNames === 'string' ? stats.trackEventNames : '',
      lastServiceStateSuccess: !!stats.lastServiceStateSuccess,
      lastServiceStateSize: Number(stats.lastServiceStateSize || 0),
      serviceStateProducerCount: Number(stats.serviceStateProducerCount || 0),
      serviceStateDataSourceCount: Number(stats.serviceStateDataSourceCount || 0),
      serviceStateTracingSessionCount: Number(stats.serviceStateTracingSessionCount || 0),
      serviceStateSupportsTracingSessions: !!stats.serviceStateSupportsTracingSessions,
      serviceStateNumSessions: Number(stats.serviceStateNumSessions || 0),
      serviceStateNumSessionsStarted: Number(stats.serviceStateNumSessionsStarted || 0)
    };
  } catch (error) {
    return null;
  }
}

function getNativePerfettoTraceDataBase64() {
  const binding = getNativeTracing();
  if (!binding || typeof binding.getPerfettoTraceData !== 'function')
    return '';

  try {
    const traceData = binding.getPerfettoTraceData();
    return typeof traceData === 'string' ? traceData : '';
  } catch (error) {
    return '';
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
  const nativePerfetto = getNativePerfettoStats();
  const nativePerfettoTraceData = getNativePerfettoTraceDataBase64();
  if (nativePerfetto && nativePerfettoTraceData)
    nativePerfetto.traceDataBase64 = nativePerfettoTraceData;
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
      nativeTraceEventCount: nativeEvents.length,
      nativePerfetto
    }
  };
  fs.writeFileSync(filePath, JSON.stringify(payload, null, 2));
  return filePath;
}

const contentTracing = {
  getCategories() {
    return Promise.resolve(categories.slice());
  },

  enableHeapProfiling(options) {
    if (recording)
      return Promise.reject(new Error('contentTracing.enableHeapProfiling must be called before startRecording'));

    try {
      heapProfilingOptions = normalizeHeapProfilingOptions(options);
      return Promise.resolve();
    } catch (error) {
      return Promise.reject(error);
    }
  },

  startRecording(options) {
    if (recording)
      return Promise.reject(new Error('contentTracing is already recording'));

    traceConfig = normalizeOptions(options);
    applyHeapProfilingOptions(traceConfig);
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
    const eventName = name || 'contentTracing.snapshot';
    const payload = addSnapshot(eventName, data);
    const binding = getNativeTracing();
    if (binding && typeof binding.recordInstantEvent === 'function') {
      try {
        binding.recordInstantEvent(eventName, JSON.stringify(payload));
      } catch (error) {
      }
    }
  }
};

module.exports = contentTracing;
