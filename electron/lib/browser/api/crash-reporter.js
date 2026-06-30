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

function ensureDirectory(dir) {
  fs.mkdirSync(dir, { recursive: true });
  return dir;
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

function reportPath(kind) {
  ensureDirectory(crashesDirectory);
  const safeKind = String(kind || 'crash').replace(/[^a-z0-9_.-]/gi, '_');
  return path.join(crashesDirectory, `${safeKind}-${process.pid}-${Date.now()}.json`);
}

function diagnosticReportPath(kind) {
  ensureDirectory(crashesDirectory);
  const safeKind = String(kind || 'crash').replace(/[^a-z0-9_.-]/gi, '_');
  return path.join(crashesDirectory, `${safeKind}-${process.pid}-${Date.now()}.diagnostic.json`);
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

function uploadReport(report) {
  if (!optionsState || !optionsState.uploadToServer || !optionsState.submitURL)
    return;

  let parsed;
  try {
    parsed = new url.URL(optionsState.submitURL);
  } catch (e) {
    return;
  }

  const body = JSON.stringify(report);
  const transport = parsed.protocol === 'https:' ? https : http;
  const request = transport.request({
    method: 'POST',
    hostname: parsed.hostname,
    port: parsed.port || undefined,
    path: `${parsed.pathname}${parsed.search}`,
    headers: {
      'content-type': 'application/json',
      'content-length': Buffer.byteLength(body)
    },
    timeout: 3000
  }, function(response) {
    response.resume();
    response.on('end', function() {
      if (response.statusCode >= 200 && response.statusCode < 300) {
        uploadedReports.push(Object.assign({}, report, {
          upload_date: new Date().toISOString()
        }));
      }
    });
  });
  request.on('error', function() {});
  request.on('timeout', function() {
    request.destroy();
  });
  request.end(body);
}

function writeReport(kind, error, origin) {
  if (!started)
    return null;

  const diagnosticReport = writeDiagnosticReport(kind, error);
  const stack = error && error.stack ? String(error.stack) : String(error || '');
  const report = {
    id: `${process.pid}-${Date.now()}`,
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
    path: ''
  };
  const filePath = reportPath(report.kind);
  fs.writeFileSync(filePath, JSON.stringify(report, null, 2));
  report.path = filePath;
  lastCrashReport = report;
  uploadReport(report);
  return report;
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
    ensureDirectory(crashesDirectory);
    configureDiagnosticReport();
    started = true;
    installHandlers();
  },

  getLastCrashReport() {
    return lastCrashReport;
  },

  getUploadedReports() {
    return uploadedReports.slice();
  },

  getUploadToServer() {
    return !!(optionsState && optionsState.uploadToServer);
  },

  setUploadToServer(uploadToServer) {
    if (!optionsState)
      optionsState = normalizeStartOptions({ uploadToServer: false });
    optionsState.uploadToServer = !!uploadToServer;
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
    ensureDirectory(crashesDirectory);
    configureDiagnosticReport();
  },

  _writeReportForTesting(kind, error, origin) {
    return writeReport(kind, error, origin);
  }
};

module.exports = crashReporter;
