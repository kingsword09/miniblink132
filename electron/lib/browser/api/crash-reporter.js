'use strict';

const fs = require('fs');
const http = require('http');
const https = require('https');
const os = require('os');
const path = require('path');
const url = require('url');

let started = false;
let optionsState = null;
let extraParameters = Object.create(null);
let crashesDirectory = path.join(os.tmpdir(), 'miniblink-crashes');
let lastCrashReport = null;
let uploadedReports = [];
let handlersInstalled = false;
let nativeCrashReporter = null;
let nativeCrashService = null;
let nativeCrashServiceExitHookInstalled = false;
const uploadingReports = new Set();

function ensureDirectory(dir) {
  fs.mkdirSync(dir, { recursive: true });
  return dir;
}

function crashDatabaseDirectory() {
  return path.join(crashesDirectory, 'reports');
}

function crashDatabaseStateDirectory(state) {
  return path.join(crashDatabaseDirectory(), state);
}

function ensureCrashDatabase() {
  ensureDirectory(crashesDirectory);
  ['new', 'pending', 'completed', 'uploaded'].forEach(function(state) {
    ensureDirectory(crashDatabaseStateDirectory(state));
  });
}

function safeReportId(value) {
  return String(value || '').replace(/[^a-z0-9_.-]/gi, '_');
}

function createReportId(kind) {
  const safeKind = safeReportId(kind || 'crash');
  return `${safeKind}-${process.pid}-${Date.now()}-${Math.floor(Math.random() * 0x100000000).toString(16)}`;
}

function reportDatabasePath(state, id) {
  return path.join(crashDatabaseStateDirectory(state), `${safeReportId(id)}.json`);
}

function writeJsonAtomic(filePath, value) {
  ensureDirectory(path.dirname(filePath));
  const temporaryPath = `${filePath}.${process.pid}.tmp`;
  fs.writeFileSync(temporaryPath, JSON.stringify(value, null, 2));
  fs.renameSync(temporaryPath, filePath);
}

function readJsonFile(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch (error) {
    return null;
  }
}

function readReportsFromState(state) {
  const directory = crashDatabaseStateDirectory(state);
  try {
    return fs.readdirSync(directory)
      .filter(function(name) { return name.endsWith('.json'); })
      .map(function(name) { return readJsonFile(path.join(directory, name)); })
      .filter(Boolean)
      .sort(function(left, right) {
        return String(left.date || '').localeCompare(String(right.date || ''));
      });
  } catch (error) {
    return [];
  }
}

function refreshUploadedReportsFromDatabase() {
  uploadedReports = readReportsFromState('uploaded');
  return uploadedReports;
}

function latestCompletedReportFromDatabase() {
  const reports = readReportsFromState('completed');
  return reports.length ? reports[reports.length - 1] : null;
}

function recordReportState(report, state) {
  ensureCrashDatabase();
  const database = Object.assign({}, report.database || {}, {
    state,
    updatedAt: new Date().toISOString()
  });
  const stored = Object.assign({}, report, { database });
  const filePath = reportDatabasePath(state, stored.id);
  stored.path = state === 'completed' ? filePath : (report.path || filePath);
  writeJsonAtomic(filePath, stored);
  return stored;
}

function removeReportState(report, state) {
  if (!report || !report.id)
    return;
  try {
    fs.unlinkSync(reportDatabasePath(state, report.id));
  } catch (error) {
  }
}

function completeReport(report) {
  const completed = recordReportState(report, 'completed');
  removeReportState(completed, 'new');
  if (optionsState && optionsState.uploadToServer)
    recordReportState(completed, 'pending');
  return completed;
}

function enqueueCompletedReportsForUpload() {
  const uploaded = new Set(readReportsFromState('uploaded').map(function(report) {
    return report.id;
  }));
  const pending = new Set(readReportsFromState('pending').map(function(report) {
    return report.id;
  }));
  readReportsFromState('completed').forEach(function(report) {
    if (!uploaded.has(report.id) && !pending.has(report.id))
      recordReportState(report, 'pending');
  });
}

function recordUploadComplete(report, uploadDate) {
  const uploaded = Object.assign({}, report, {
    upload_date: uploadDate || new Date().toISOString()
  });
  removeReportState(uploaded, 'pending');
  const stored = recordReportState(uploaded, 'uploaded');
  refreshUploadedReportsFromDatabase();
  return stored;
}

function multipartHeaderValue(value) {
  return String(value || '').replace(/[\"\r\n]/g, '_');
}

function appendMultipartField(chunks, boundary, name, value) {
  chunks.push(Buffer.from(`--${boundary}\r\n`));
  chunks.push(Buffer.from(`Content-Disposition: form-data; name="${multipartHeaderValue(name)}"\r\n\r\n`));
  chunks.push(Buffer.from(String(value == null ? '' : value)));
  chunks.push(Buffer.from('\r\n'));
}

function appendMultipartFile(chunks, boundary, name, filePath) {
  const fileName = path.basename(filePath);
  chunks.push(Buffer.from(`--${boundary}\r\n`));
  chunks.push(Buffer.from(`Content-Disposition: form-data; name="${multipartHeaderValue(name)}"; filename="${multipartHeaderValue(fileName)}"\r\n`));
  chunks.push(Buffer.from('Content-Type: application/octet-stream\r\n\r\n'));
  chunks.push(fs.readFileSync(filePath));
  chunks.push(Buffer.from('\r\n'));
}

function minidumpUploadPath(report) {
  const filePath = report && (report.minidump || (report.nativeMinidump && report.nativeMinidump.path));
  if (!filePath)
    return '';
  try {
    const stat = fs.statSync(filePath);
    return stat.isFile() ? filePath : '';
  } catch (error) {
    return '';
  }
}

function buildMultipartUpload(report) {
  const boundary = `----miniblink-crash-${process.pid}-${Date.now()}-${Math.floor(Math.random() * 0x100000000).toString(16)}`;
  const chunks = [];
  appendMultipartField(chunks, boundary, 'prod', report.productName || '');
  appendMultipartField(chunks, boundary, 'companyName', report.companyName || '');
  appendMultipartField(chunks, boundary, 'id', report.id || '');
  appendMultipartField(chunks, boundary, 'process_type', report.process_type || 'browser');
  appendMultipartField(chunks, boundary, 'platform', report.platform || process.platform);
  appendMultipartField(chunks, boundary, 'arch', report.arch || process.arch);
  Object.keys(report.extra || {}).forEach(function(key) {
    appendMultipartField(chunks, boundary, key, report.extra[key]);
  });
  appendMultipartField(chunks, boundary, 'payload', JSON.stringify(report));

  const minidumpPath = minidumpUploadPath(report);
  if (minidumpPath)
    appendMultipartFile(chunks, boundary, 'upload_file_minidump', minidumpPath);

  chunks.push(Buffer.from(`--${boundary}--\r\n`));
  return {
    body: Buffer.concat(chunks),
    contentType: `multipart/form-data; boundary=${boundary}`,
    hasMinidump: !!minidumpPath
  };
}

function processPendingUploads() {
  if (!optionsState || !optionsState.uploadToServer || !optionsState.submitURL)
    return;
  readReportsFromState('pending').forEach(function(report) {
    uploadReport(report);
  });
}

function getCrashDatabaseStatus() {
  ensureCrashDatabase();
  return {
    path: crashDatabaseDirectory(),
    new: readReportsFromState('new').length,
    pending: readReportsFromState('pending').length,
    completed: readReportsFromState('completed').length,
    uploaded: readReportsFromState('uploaded').length
  };
}

function stringMap(value) {
  const result = Object.create(null);
  if (!value || typeof value !== 'object')
    return result;
  Object.keys(value).forEach(function(key) {
    if (value[key] != null)
      result[key] = String(value[key]);
  });
  return result;
}

function diagnosticReportPath(kind) {
  ensureCrashDatabase();
  const safeKind = safeReportId(kind || 'crash');
  return path.join(crashesDirectory, `${safeKind}-${process.pid}-${Date.now()}.diagnostic.json`);
}

function minidumpPath(kind) {
  ensureCrashDatabase();
  const safeKind = safeReportId(kind || 'crash');
  return path.join(crashesDirectory, `${safeKind}-${process.pid}-${Date.now()}.dmp`);
}

function getNativeCrashReporter() {
  if (nativeCrashReporter !== null)
    return nativeCrashReporter;

  nativeCrashReporter = false;
  try {
    if (process && typeof process._linkedBinding === 'function')
      nativeCrashReporter = process._linkedBinding('electron_common_crash_reporter');
  } catch (error) {
    nativeCrashReporter = false;
  }
  return nativeCrashReporter || null;
}

function configureDiagnosticReport() {
  if (!process.report || typeof process.report !== 'object')
    return;

  try {
    process.report.directory = crashesDirectory;
  } catch (error) {
  }

  try {
    process.report.reportOnUncaughtException = true;
  } catch (error) {
  }

  try {
    process.report.reportOnFatalError = true;
  } catch (error) {
  }
}

function writeDiagnosticReport(kind, error) {
  if (!process.report || typeof process.report.writeReport !== 'function')
    return null;

  const filePath = diagnosticReportPath(kind);
  const fileName = path.basename(filePath);
  try {
    const written = process.report.writeReport(fileName, error || new Error(String(kind || 'crash')));
    if (!written)
      return filePath;
    return path.isAbsolute(written) ? written : path.join(crashesDirectory, written);
  } catch (diagnosticError) {
    return null;
  }
}

function writeNativeMinidump(kind) {
  const binding = getNativeCrashReporter();
  if (!binding)
    return null;

  const filePath = minidumpPath(kind);
  if (nativeCrashService && nativeCrashService.running && typeof binding.requestCrashServiceDump === 'function') {
    try {
      const result = binding.requestCrashServiceDump(filePath);
      if (result && result.path) {
        return {
          path: result.path,
          size: result.size || 0,
          streamCount: result.streamCount || 0,
          threadCount: result.threadCount || 0,
          moduleCount: result.moduleCount || 0,
          servicePid: result.servicePid || 0,
          outOfProcess: result.outOfProcess === true
        };
      }
    } catch (error) {
    }
  }

  if (typeof binding.writeMinidump !== 'function')
    return null;

  try {
    const result = binding.writeMinidump(filePath);
    if (!result || !result.path)
      return null;
    return {
      path: result.path,
      size: result.size || 0,
      streamCount: result.streamCount || 0,
      threadCount: result.threadCount || 0,
      moduleCount: result.moduleCount || 0
    };
  } catch (error) {
    return null;
  }
}

function installNativeHandlers() {
  const binding = getNativeCrashReporter();
  if (!binding || typeof binding.installSignalHandlers !== 'function')
    return false;

  try {
    const result = binding.installSignalHandlers(crashesDirectory);
    if (result && typeof result === 'object')
      nativeCrashService = result;
    if (!nativeCrashServiceExitHookInstalled && typeof binding.stopCrashService === 'function' && typeof process.once === 'function') {
      nativeCrashServiceExitHookInstalled = true;
      process.once('exit', function() {
        try {
          binding.stopCrashService();
        } catch (error) {
        }
      });
    }
    return !!result;
  } catch (error) {
    return false;
  }
}

function uploadReport(report) {
  if (!optionsState || !optionsState.uploadToServer || !optionsState.submitURL)
    return;
  if (!report || !report.id || uploadingReports.has(report.id))
    return;

  let parsed;
  try {
    parsed = new url.URL(optionsState.submitURL);
  } catch (e) {
    return;
  }

  const pendingReport = recordReportState(report, 'pending');
  const upload = buildMultipartUpload(pendingReport);
  const transport = parsed.protocol === 'https:' ? https : http;
  uploadingReports.add(pendingReport.id);
  const request = transport.request({
    method: 'POST',
    hostname: parsed.hostname,
    port: parsed.port || undefined,
    path: `${parsed.pathname}${parsed.search}`,
    headers: {
      'content-type': upload.contentType,
      'content-length': upload.body.length
    },
    timeout: 3000
  }, function(response) {
    response.resume();
    response.on('end', function() {
      uploadingReports.delete(pendingReport.id);
      if (response.statusCode >= 200 && response.statusCode < 300) {
        recordUploadComplete(pendingReport, new Date().toISOString());
      }
    });
  });
  request.on('error', function() {
    uploadingReports.delete(pendingReport.id);
  });
  request.on('timeout', function() {
    uploadingReports.delete(pendingReport.id);
    request.destroy();
  });
  request.end(upload.body);
}

function writeReport(kind, error, origin) {
  if (!started)
    return null;

  const diagnosticReport = writeDiagnosticReport(kind, error);
  const nativeMinidump = writeNativeMinidump(kind);
  const stack = error && error.stack ? String(error.stack) : String(error || '');
  const reportId = createReportId(kind);
  const report = {
    id: reportId,
    date: new Date().toISOString(),
    kind: kind || 'exception',
    origin: origin || '',
    process_type: 'browser',
    productName: optionsState.productName,
    companyName: optionsState.companyName,
    extra: Object.assign({}, extraParameters),
    stack,
    platform: process.platform,
    arch: process.arch,
    node: process.version,
    diagnosticReport,
    minidump: nativeMinidump ? nativeMinidump.path : null,
    nativeMinidump,
    path: '',
    database: {
      path: crashDatabaseDirectory(),
      state: 'new',
      createdAt: new Date().toISOString(),
      updatedAt: new Date().toISOString()
    }
  };
  const newReport = recordReportState(report, 'new');
  const completedReport = completeReport(newReport);
  lastCrashReport = completedReport;
  if (optionsState && optionsState.uploadToServer)
    uploadReport(completedReport);
  return completedReport;
}

function installHandlers() {
  if (handlersInstalled)
    return;
  handlersInstalled = true;

  if (typeof process.on === 'function') {
    process.on('uncaughtExceptionMonitor', function(error, origin) {
      writeReport('uncaughtException', error, origin);
    });
    process.on('unhandledRejection', function(reason) {
      writeReport('unhandledRejection', reason, 'unhandledRejection');
    });
  }
}

function normalizeStartOptions(options) {
  if (!options || typeof options !== 'object')
    throw new TypeError('crashReporter.start options must be an object');

  const uploadToServer = options.uploadToServer !== false;
  if (uploadToServer && !options.submitURL)
    throw new TypeError('crashReporter.start requires submitURL when uploadToServer is true');

  const productName = options.productName || options.product || 'miniblink';
  const companyName = options.companyName || options.company || '';
  const directory = options.crashesDirectory || options.crashReportDirectory || options.crashReportFolder;
  if (directory)
    crashesDirectory = path.resolve(String(directory));

  return {
    submitURL: options.submitURL ? String(options.submitURL) : '',
    uploadToServer,
    productName: String(productName),
    companyName: String(companyName),
    compress: !!options.compress,
    rateLimit: options.rateLimit !== false,
    ignoreSystemCrashHandler: !!options.ignoreSystemCrashHandler,
    globalExtra: stringMap(options.globalExtra || options.extra || options.extraParameters)
  };
}

const crashReporter = {
  start(options) {
    optionsState = normalizeStartOptions(options);
    extraParameters = Object.assign(Object.create(null), optionsState.globalExtra);
    ensureCrashDatabase();
    refreshUploadedReportsFromDatabase();
    lastCrashReport = latestCompletedReportFromDatabase();
    configureDiagnosticReport();
    installNativeHandlers();
    started = true;
    installHandlers();
    processPendingUploads();
  },

  getLastCrashReport() {
    return lastCrashReport || latestCompletedReportFromDatabase();
  },

  getUploadedReports() {
    return refreshUploadedReportsFromDatabase().slice();
  },

  getUploadToServer() {
    return !!(optionsState && optionsState.uploadToServer);
  },

  setUploadToServer(uploadToServer) {
    if (!optionsState)
      optionsState = normalizeStartOptions({ uploadToServer: false });
    optionsState.uploadToServer = !!uploadToServer;
    if (optionsState.uploadToServer) {
      enqueueCompletedReportsForUpload();
      processPendingUploads();
    }
  },

  getParameters() {
    return Object.assign({}, extraParameters);
  },

  addExtraParameter(key, value) {
    if (!key || typeof key !== 'string')
      throw new TypeError('crashReporter.addExtraParameter key must be a non-empty string');
    extraParameters[key] = String(value);
  },

  removeExtraParameter(key) {
    if (!key || typeof key !== 'string')
      throw new TypeError('crashReporter.removeExtraParameter key must be a non-empty string');
    delete extraParameters[key];
  },

  getCrashesDirectory() {
    return crashesDirectory;
  },

  getCrashReportFolder() {
    return crashesDirectory;
  },

  setCrashReportFolder(folder) {
    if (!folder || typeof folder !== 'string')
      throw new TypeError('crashReporter.setCrashReportFolder folder must be a non-empty string');
    crashesDirectory = path.resolve(folder);
    ensureCrashDatabase();
    refreshUploadedReportsFromDatabase();
    lastCrashReport = latestCompletedReportFromDatabase();
    configureDiagnosticReport();
    installNativeHandlers();
  },

  _writeReportForTesting(kind, error, origin) {
    return writeReport(kind, error, origin);
  },

  _getNativeCrashServiceForTesting() {
    const binding = getNativeCrashReporter();
    if (binding && typeof binding.getCrashServiceStatus === 'function') {
      try {
        nativeCrashService = binding.getCrashServiceStatus();
      } catch (error) {
      }
    }
    return nativeCrashService;
  },

  _stopNativeCrashServiceForTesting() {
    const binding = getNativeCrashReporter();
    if (binding && typeof binding.stopCrashService === 'function') {
      try {
        return binding.stopCrashService();
      } catch (error) {
      }
    }
    return false;
  },

  _getCrashReportDatabaseForTesting() {
    return getCrashDatabaseStatus();
  },

  _getPendingReportsForTesting() {
    ensureCrashDatabase();
    return readReportsFromState('pending');
  },

  _recordUploadCompleteForTesting(report) {
    return recordUploadComplete(report);
  },

  _processPendingUploadsForTesting() {
    processPendingUploads();
  }
};

module.exports = crashReporter;
