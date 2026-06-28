const EventEmitter = require('events').EventEmitter;
const sessionBinding = process._linkedBinding('electron_browser_session');
const Session = sessionBinding.Session;

Object.setPrototypeOf(Session.prototype, EventEmitter.prototype);

//--
process._linkedBinding('electron_browser_webrequest');
Session.defaultSession = Session.fromPartition("");
//--
const downloaditemBinding = process._linkedBinding('electron_browser_downloaditem');
const DownloadItem = downloaditemBinding.DownloadItem;
Object.setPrototypeOf(DownloadItem.prototype, EventEmitter.prototype);
//--

exports.session = Session;
