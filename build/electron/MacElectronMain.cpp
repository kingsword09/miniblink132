#include "base/message_loop/message_pump_type.h"
#include "base/command_line.h"
#include "base/task/single_thread_task_executor.h"
#include "electron/common/AtomCommandLine.h"
#include "electron/common/gin_helper/per_isolate_data.h"
#include "linux/windows.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libuv/include/uv.h"
#include "v8/include/libplatform/libplatform.h"
#include "v8/include/v8.h"

#include <IOKit/pwr_mgt/IOPMLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

extern bool g_isElectronMode;

extern "C" void nodeModuleInitRegister(void);
extern "C" void _register_electron_common_content_tracing(void);
extern "C" bool electronMacNodeBridgeHasLinkedModule(const char* name);
extern "C" bool electronMacNodeBridgeGetLinkedModuleRegistration(const char* name, node::addon_context_register_func* registerFunc, void** priv);
extern "C" bool electronMacNodeBridgeGetLinkedBinding(const char* name, v8::Local<v8::Context> context, v8::Local<v8::Value>* out);
extern "C" bool electronMacNativeThemeSetAppearanceForTesting(bool dark);
extern "C" void electronMacNativeThemeResetAppearanceForTesting();
extern "C" void MacDispatchPowerMonitorMessageForTesting(UINT event);
extern "C" void MacGetPowerSaveBlockerAssertionStateForTesting(BOOL* systemOn, BOOL* displayOn);
extern "C" bool electronWindowListCloseAllWindowsSmokeForTesting();

namespace {

bool hasArg(int argc, char** argv, const char* needle)
{
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && strcmp(argv[i], needle) == 0)
            return true;
    }
    return false;
}

void printNodeInitErrors(const node::InitializationResult& init)
{
    for (const std::string& error : init.errors())
        fprintf(stderr, "node init error: %s\n", error.c_str());
}

void printV8Exception(v8::Isolate* isolate, const v8::TryCatch& tryCatch)
{
    v8::HandleScope handleScope(isolate);
    v8::String::Utf8Value exception(isolate, tryCatch.Exception());
    fprintf(stderr, "v8 exception: %s\n", *exception ? *exception : "<unknown>");
    v8::Local<v8::Message> message = tryCatch.Message();
    if (!message.IsEmpty()) {
        v8::Local<v8::Value> stackTrace;
        if (tryCatch.StackTrace(isolate->GetCurrentContext()).ToLocal(&stackTrace) && stackTrace->IsString()) {
            v8::String::Utf8Value stack(isolate, stackTrace);
            if (*stack && **stack)
                fprintf(stderr, "v8 stack: %s\n", *stack);
        }
    }
}

bool getObjectProperty(v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char* name, v8::Local<v8::Value>* out)
{
    v8::Isolate* isolate = context->GetIsolate();
    v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name).ToLocalChecked();
    return object->Get(context, key).ToLocal(out);
}

bool requireFunctionProperty(v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char* objectName, const char* propertyName)
{
    v8::Local<v8::Value> value;
    if (!getObjectProperty(context, object, propertyName, &value)) {
        fprintf(stderr, "failed to read %s.%s\n", objectName, propertyName);
        return false;
    }
    if (!value->IsFunction()) {
        fprintf(stderr, "%s.%s is not a function\n", objectName, propertyName);
        return false;
    }
    return true;
}

bool requireBinding(v8::Local<v8::Context> context, const char* name, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Value> exports;
    if (!electronMacNodeBridgeGetLinkedBinding(name, context, &exports)) {
        fprintf(stderr, "process._linkedBinding('%s') failed\n", name);
        return false;
    }
    if (!exports->IsObject()) {
        fprintf(stderr, "process._linkedBinding('%s') did not return an object\n", name);
        return false;
    }

    *out = exports.As<v8::Object>();
    return true;
}

bool runV8Script(v8::Local<v8::Context> context, const char* scriptSource)
{
    v8::Isolate* isolate = context->GetIsolate();
    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, scriptSource).ToLocalChecked();
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    bool ok = v8::Script::Compile(context, source).ToLocal(&script)
        && script->Run(context).ToLocal(&result)
        && result->BooleanValue(isolate);
    if (!ok && tryCatch.HasCaught())
        printV8Exception(isolate, tryCatch);
    return ok;
}

void drainNodeLoop(node::CommonEnvironmentSetup* setup, node::MultiIsolatePlatform* platform, v8::Isolate* isolate, int iterations)
{
    if (!setup || !platform || !isolate)
        return;
    for (int i = 0; i < iterations; ++i) {
        uv_run(setup->event_loop(), UV_RUN_NOWAIT);
        platform->DrainTasks(isolate);
    }
}

void linkedBindingApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1 || !info[0]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isolate, "binding name is required").ToLocalChecked()));
        return;
    }

    v8::String::Utf8Value name(isolate, info[0]);
    v8::Local<v8::Value> exports;
    if (!*name || !electronMacNodeBridgeGetLinkedBinding(*name, context, &exports)) {
        isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "linked binding is missing").ToLocalChecked()));
        return;
    }

    info.GetReturnValue().Set(exports);
}

bool runLinkedBindingRuntimeSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();

    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();

    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams createParams;
    createParams.array_buffer_allocator = allocator.get();

    v8::Isolate* isolate = v8::Isolate::New(createParams);
    bool ok = false;
    {
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, allocator.get());

        v8::Local<v8::Object> process = v8::Object::New(isolate);
        process->Set(context,
            v8::String::NewFromUtf8(isolate, "_linkedBinding").ToLocalChecked(),
            v8::FunctionTemplate::New(isolate, linkedBindingApi)->GetFunction(context).ToLocalChecked()).ToChecked();
        context->Global()->Set(context, v8::String::NewFromUtf8(isolate, "process").ToLocalChecked(), process).ToChecked();

        v8::Local<v8::Object> nativeTheme;
        v8::Local<v8::Object> app;
        v8::Local<v8::Object> powerMonitor;
        v8::Local<v8::Object> globalShortcut;
        v8::Local<v8::Object> powerSaveBlocker;
        v8::Local<v8::Object> screen;
        v8::Local<v8::Object> menu;
        v8::Local<v8::Object> nativeImage;
        v8::Local<v8::Object> clipboard;
        v8::Local<v8::Object> shell;
        v8::Local<v8::Object> dialog;
        v8::Local<v8::Object> tray;
        v8::Local<v8::Object> protocol;
        v8::Local<v8::Object> commandLine;
        v8::Local<v8::Object> safeStorage;
        v8::Local<v8::Object> systemPreferences;
        v8::Local<v8::Object> session;
        v8::Local<v8::Object> webRequest;
        v8::Local<v8::Object> downloadItem;
        v8::Local<v8::Object> messagePort;
        v8::Local<v8::Object> browserView;
        v8::Local<v8::Object> webFrameMain;
        v8::Local<v8::Object> utilityProcess;
        v8::Local<v8::Object> webContents;
        v8::Local<v8::Object> browserWindow;
        v8::Local<v8::Object> rendererIpc;
        v8::Local<v8::Object> rendererContextBridge;
        v8::Local<v8::Object> rendererWebFrame;
        v8::Local<v8::Object> features;
        v8::Local<v8::Object> v8Util;
        v8::Local<v8::Object> intlCollator;
        v8::Local<v8::Object> asar;
        v8::Local<v8::Object> contentTracing;

        ok = requireBinding(context, "electron_browser_native_theme", &nativeTheme)
            && requireFunctionProperty(context, nativeTheme, "electron_browser_native_theme", "shouldUseDarkColors")
            && requireFunctionProperty(context, nativeTheme, "electron_browser_native_theme", "startWatching")
            && requireFunctionProperty(context, nativeTheme, "electron_browser_native_theme", "stopWatching")
            && requireBinding(context, "electron_browser_app", &app)
            && requireFunctionProperty(context, app, "electron_browser_app", "App")
            && requireBinding(context, "electron_browser_powermonitor", &powerMonitor)
            && requireFunctionProperty(context, powerMonitor, "electron_browser_powermonitor", "ApiPowerMonitor")
            && requireBinding(context, "electron_browser_global_shortcut", &globalShortcut)
            && requireFunctionProperty(context, globalShortcut, "electron_browser_global_shortcut", "register")
            && requireFunctionProperty(context, globalShortcut, "electron_browser_global_shortcut", "unregister")
            && requireFunctionProperty(context, globalShortcut, "electron_browser_global_shortcut", "unregisterAll")
            && requireBinding(context, "electron_browser_power_save_blocker", &powerSaveBlocker)
            && requireFunctionProperty(context, powerSaveBlocker, "electron_browser_power_save_blocker", "setExecutionState")
            && requireBinding(context, "electron_common_screen", &screen)
            && requireFunctionProperty(context, screen, "electron_common_screen", "Screen")
            && requireBinding(context, "electron_browser_menu", &menu)
            && requireFunctionProperty(context, menu, "electron_browser_menu", "Menu")
            && requireFunctionProperty(context, menu, "electron_browser_menu", "_clearApplicationMenu")
            && requireFunctionProperty(context, menu, "electron_browser_menu", "_sendActionToFirstResponder")
            && requireBinding(context, "electron_common_nativeImage", &nativeImage)
            && requireFunctionProperty(context, nativeImage, "electron_common_nativeImage", "NativeImage")
            && requireBinding(context, "electron_common_clipboard", &clipboard)
            && requireFunctionProperty(context, clipboard, "electron_common_clipboard", "Clipboard")
            && requireBinding(context, "electron_common_shell", &shell)
            && requireBinding(context, "electron_browser_dialog", &dialog)
            && requireFunctionProperty(context, dialog, "electron_browser_dialog", "Dialog")
            && requireBinding(context, "electron_browser_tray", &tray)
            && requireFunctionProperty(context, tray, "electron_browser_tray", "Tray")
            && requireBinding(context, "electron_browser_protocol", &protocol)
            && requireFunctionProperty(context, protocol, "electron_browser_protocol", "Protocol")
            && requireBinding(context, "electron_browser_commandline", &commandLine)
            && requireFunctionProperty(context, commandLine, "electron_browser_commandline", "ApiCommandLine")
            && requireBinding(context, "electron_browser_safe_storage", &safeStorage)
            && requireFunctionProperty(context, safeStorage, "electron_browser_safe_storage", "encryptString")
            && requireFunctionProperty(context, safeStorage, "electron_browser_safe_storage", "decryptString")
            && requireBinding(context, "electron_browser_system_preferences", &systemPreferences)
            && requireFunctionProperty(context, systemPreferences, "electron_browser_system_preferences", "getAccentColor")
            && requireFunctionProperty(context, systemPreferences, "electron_browser_system_preferences", "getColor")
            && requireFunctionProperty(context, systemPreferences, "electron_browser_system_preferences", "getMediaAccessStatus")
            && requireFunctionProperty(context, systemPreferences, "electron_browser_system_preferences", "askForMediaAccess")
            && requireBinding(context, "electron_browser_session", &session)
            && requireFunctionProperty(context, session, "electron_browser_session", "Session")
            && requireBinding(context, "electron_browser_webrequest", &webRequest)
            && requireFunctionProperty(context, webRequest, "electron_browser_webrequest", "WebRequest")
            && requireBinding(context, "electron_browser_downloaditem", &downloadItem)
            && requireFunctionProperty(context, downloadItem, "electron_browser_downloaditem", "DownloadItem")
            && requireBinding(context, "electron_browser_message_port", &messagePort)
            && requireFunctionProperty(context, messagePort, "electron_browser_message_port", "createPair")
            && requireBinding(context, "electron_browser_browserview", &browserView)
            && requireFunctionProperty(context, browserView, "electron_browser_browserview", "BrowserView")
            && requireBinding(context, "electron_browser_web_frame_main", &webFrameMain)
            && requireFunctionProperty(context, webFrameMain, "electron_browser_web_frame_main", "WebFrameMain")
            && requireFunctionProperty(context, webFrameMain, "electron_browser_web_frame_main", "fromId")
            && requireFunctionProperty(context, webFrameMain, "electron_browser_web_frame_main", "fromIdOrNull")
            && requireBinding(context, "electron_browser_utility_process", &utilityProcess)
            && requireFunctionProperty(context, utilityProcess, "electron_browser_utility_process", "_fork")
            && requireBinding(context, "electron_browser_web_contents", &webContents)
            && requireFunctionProperty(context, webContents, "electron_browser_web_contents", "WebContents")
            && requireBinding(context, "electron_browser_browserwindow", &browserWindow)
            && requireFunctionProperty(context, browserWindow, "electron_browser_browserwindow", "BrowserWindow")
            && requireBinding(context, "electron_renderer_ipc", &rendererIpc)
            && requireFunctionProperty(context, rendererIpc, "electron_renderer_ipc", "ipcRenderer")
            && requireBinding(context, "electron_renderer_contextbridge", &rendererContextBridge)
            && requireFunctionProperty(context, rendererContextBridge, "electron_renderer_contextbridge", "exposeInMainWorld")
            && requireBinding(context, "electron_renderer_webframe", &rendererWebFrame)
            && requireFunctionProperty(context, rendererWebFrame, "electron_renderer_webframe", "WebFrame")
            && requireBinding(context, "electron_common_features", &features)
            && requireFunctionProperty(context, features, "electron_common_features", "isViewApiEnabled")
            && requireBinding(context, "electron_common_v8_util", &v8Util)
            && requireFunctionProperty(context, v8Util, "electron_common_v8_util", "getHiddenValue")
            && requireFunctionProperty(context, v8Util, "electron_common_v8_util", "takeHeapSnapshot")
            && requireBinding(context, "electron_common_intl_collator", &intlCollator)
            && requireFunctionProperty(context, intlCollator, "electron_common_intl_collator", "IntlCollator")
            && requireBinding(context, "electron_common_asar", &asar)
            && requireFunctionProperty(context, asar, "electron_common_asar", "Archive")
            && requireFunctionProperty(context, asar, "electron_common_asar", "initAsarSupport")
            && requireBinding(context, "electron_common_content_tracing", &contentTracing)
            && requireFunctionProperty(context, contentTracing, "electron_common_content_tracing", "startRecording")
            && requireFunctionProperty(context, contentTracing, "electron_common_content_tracing", "stopRecording")
            && requireFunctionProperty(context, contentTracing, "electron_common_content_tracing", "getTraceBufferUsage")
            && requireFunctionProperty(context, contentTracing, "electron_common_content_tracing", "recordInstantEvent");

        if (ok) {
            const char scriptSource[] =
                "const theme = process._linkedBinding('electron_browser_native_theme');"
                "const app = process._linkedBinding('electron_browser_app');"
                "const psb = process._linkedBinding('electron_browser_power_save_blocker');"
                "const gs = process._linkedBinding('electron_browser_global_shortcut');"
                "const screen = process._linkedBinding('electron_common_screen');"
                "const menu = process._linkedBinding('electron_browser_menu');"
                "const nativeImage = process._linkedBinding('electron_common_nativeImage');"
                "const clipboard = process._linkedBinding('electron_common_clipboard');"
                "const shellBinding = process._linkedBinding('electron_common_shell');"
                "const dialog = process._linkedBinding('electron_browser_dialog');"
                "const tray = process._linkedBinding('electron_browser_tray');"
                "const protocol = process._linkedBinding('electron_browser_protocol');"
                "const commandLine = process._linkedBinding('electron_browser_commandline');"
                "const safeStorage = process._linkedBinding('electron_browser_safe_storage');"
                "const systemPreferences = process._linkedBinding('electron_browser_system_preferences');"
                "const session = process._linkedBinding('electron_browser_session');"
                "const webRequest = process._linkedBinding('electron_browser_webrequest');"
                "const downloadItem = process._linkedBinding('electron_browser_downloaditem');"
                "const messagePort = process._linkedBinding('electron_browser_message_port');"
                "const browserView = process._linkedBinding('electron_browser_browserview');"
                "const webFrameMain = process._linkedBinding('electron_browser_web_frame_main');"
                "const utilityProcess = process._linkedBinding('electron_browser_utility_process');"
                "const webContents = process._linkedBinding('electron_browser_web_contents');"
                "const browserWindow = process._linkedBinding('electron_browser_browserwindow');"
                "const rendererIpc = process._linkedBinding('electron_renderer_ipc');"
                "const rendererContextBridge = process._linkedBinding('electron_renderer_contextbridge');"
                "const rendererWebFrame = process._linkedBinding('electron_renderer_webframe');"
                "const features = process._linkedBinding('electron_common_features');"
                "const v8Util = process._linkedBinding('electron_common_v8_util');"
                "const intlCollator = process._linkedBinding('electron_common_intl_collator');"
                "const asar = process._linkedBinding('electron_common_asar');"
                "const contentTracing = process._linkedBinding('electron_common_content_tracing');"
                "if (typeof theme.shouldUseDarkColors !== 'function') throw new Error('nativeTheme');"
                "if (typeof app.App !== 'function') throw new Error('app');"
                "if (typeof psb.setExecutionState !== 'function') throw new Error('powerSaveBlocker');"
                "if (typeof gs.register !== 'function') throw new Error('globalShortcut');"
                "if (typeof screen.Screen !== 'function') throw new Error('screen');"
                "if (typeof menu.Menu !== 'function') throw new Error('menu');"
                "if (typeof nativeImage.NativeImage !== 'function') throw new Error('nativeImage');"
                "if (typeof clipboard.Clipboard !== 'function') throw new Error('clipboard');"
                "if (!shellBinding.Shell || typeof shellBinding.Shell.openPath !== 'function') throw new Error('shell');"
                "if (typeof dialog.Dialog !== 'function') throw new Error('dialog');"
                "if (typeof tray.Tray !== 'function') throw new Error('tray');"
                "if (typeof protocol.Protocol !== 'function') throw new Error('protocol');"
                "if (typeof commandLine.ApiCommandLine !== 'function') throw new Error('commandLine');"
                "if (typeof safeStorage.encryptString !== 'function') throw new Error('safeStorage');"
                "if (typeof systemPreferences.getAccentColor !== 'function') throw new Error('systemPreferences accent');"
                "if (typeof systemPreferences.getColor !== 'function') throw new Error('systemPreferences color');"
                "if (typeof systemPreferences.getMediaAccessStatus !== 'function') throw new Error('systemPreferences media status');"
                "if (typeof systemPreferences.askForMediaAccess !== 'function') throw new Error('systemPreferences media ask');"
                "if (typeof session.Session !== 'function') throw new Error('session');"
                "if (typeof webRequest.WebRequest !== 'function') throw new Error('webRequest');"
                "if (typeof downloadItem.DownloadItem !== 'function') throw new Error('downloadItem');"
                "if (typeof messagePort.createPair !== 'function') throw new Error('messagePort');"
                "if (typeof browserView.BrowserView !== 'function') throw new Error('BrowserView');"
                "if (typeof webFrameMain.WebFrameMain !== 'function') throw new Error('webFrameMain');"
                "if (typeof webFrameMain.fromId !== 'function') throw new Error('webFrameMain fromId');"
                "if (typeof webFrameMain.fromIdOrNull !== 'function') throw new Error('webFrameMain fromIdOrNull');"
                "if (typeof utilityProcess._fork !== 'function') throw new Error('utilityProcess');"
                "if (typeof webContents.WebContents !== 'function') throw new Error('webContents');"
                "if (typeof browserWindow.BrowserWindow !== 'function') throw new Error('BrowserWindow');"
                "if (typeof rendererIpc.ipcRenderer !== 'function') throw new Error('ipcRenderer');"
                "if (typeof rendererContextBridge.exposeInMainWorld !== 'function') throw new Error('contextBridge');"
                "if (typeof rendererWebFrame.WebFrame !== 'function') throw new Error('webFrame');"
                "if (typeof rendererWebFrame.WebFrame.prototype.insertCSS !== 'function') throw new Error('webFrame insertCSS');"
                "if (typeof rendererWebFrame.WebFrame.prototype.executeJavaScript !== 'function') throw new Error('webFrame executeJavaScript');"
                "if (typeof rendererWebFrame.WebFrame.prototype.setSpellCheckProvider !== 'function') throw new Error('webFrame spellcheck');"
                "if (typeof rendererWebFrame.WebFrame.prototype.insertText !== 'function') throw new Error('webFrame insertText');"
                "if (typeof features.isViewApiEnabled !== 'function') throw new Error('features');"
                "if (typeof v8Util.getHiddenValue !== 'function') throw new Error('v8Util');"
                "if (typeof v8Util.takeHeapSnapshot !== 'function') throw new Error('v8Util takeHeapSnapshot');"
                "if (typeof intlCollator.IntlCollator !== 'function') throw new Error('intlCollator');"
                "if (typeof asar.Archive !== 'function') throw new Error('asar Archive');"
                "if (typeof asar.initAsarSupport !== 'function') throw new Error('asar initAsarSupport');"
                "if (typeof contentTracing.startRecording !== 'function') throw new Error('contentTracing start');"
                "if (typeof contentTracing.stopRecording !== 'function') throw new Error('contentTracing stop');"
                "if (typeof contentTracing.getTraceBufferUsage !== 'function') throw new Error('contentTracing buffer');"
                "if (typeof contentTracing.recordInstantEvent !== 'function') throw new Error('contentTracing instant');"
                "contentTracing.startRecording('electron,miniblink', 'record-continuously');"
                "contentTracing.recordInstantEvent('runtime-smoke-native-trace', '{\"ok\":true}');"
                "const nativeTraceUsage = contentTracing.getTraceBufferUsage();"
                "if (!nativeTraceUsage || nativeTraceUsage.eventCount < 1) throw new Error('contentTracing usage');"
                "const nativeTrace = JSON.parse(contentTracing.stopRecording());"
                "if (!Array.isArray(nativeTrace.traceEvents)) throw new Error('contentTracing traceEvents');"
                "if (!nativeTrace.traceEvents.some((event) => { if (event.name !== 'runtime-smoke-native-trace' || !event.args || typeof event.args.data !== 'string') return false; try { return JSON.parse(event.args.data).ok === true; } catch (error) { return false; } })) throw new Error('contentTracing native event');"
                "true;";
            ok = runV8Script(context, scriptSource);
        }
    }

    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();

    if (!ok)
        return false;

    printf("PASS electron-linked-binding-runtime-smoke\n");
    return true;
}

bool runV8TypedArraySmoke(int argc, char** argv)
{
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();

    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();

    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams createParams;
    createParams.array_buffer_allocator = allocator.get();

    v8::Isolate* isolate = v8::Isolate::New(createParams);
    bool ok = false;
    {
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope contextScope(context);

        const char scriptSource[] =
            "const array = new Float32Array(1);"
            "array[0] = 1.5;"
            "array.length === 1 && array[0] === 1.5;";
        ok = runV8Script(context, scriptSource);
    }

    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();

    if (!ok)
        return false;

    printf("PASS electron-v8-typed-array-smoke\n");
    return true;
}

bool runV8SharedArrayBufferSmoke(int argc, char** argv)
{
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();

    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();

    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams createParams;
    createParams.array_buffer_allocator = allocator.get();

    v8::Isolate* isolate = v8::Isolate::New(createParams);
    bool ok = false;
    {
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope contextScope(context);

        const char scriptSource[] =
            "const buffer = new SharedArrayBuffer(4);"
            "const array = new Uint32Array(buffer);"
            "array[0] = 7;"
            "buffer.byteLength === 4 && array.length === 1 && array[0] === 7;";
        ok = runV8Script(context, scriptSource);
    }

    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();

    if (!ok)
        return false;

    printf("PASS electron-v8-shared-array-buffer-smoke\n");
    return true;
}

bool addNodeLinkedBinding(node::Environment* env, const char* name)
{
    node::addon_context_register_func registerFunc = nullptr;
    void* priv = nullptr;
    if (!electronMacNodeBridgeGetLinkedModuleRegistration(name, &registerFunc, &priv)) {
        fprintf(stderr, "missing linked module registration for %s\n", name);
        return false;
    }

    node::AddLinkedBinding(env, name, registerFunc, priv);
    return true;
}

bool addElectronBaseApiBindings(node::Environment* env)
{
    return addNodeLinkedBinding(env, "electron_browser_commandline")
        && addNodeLinkedBinding(env, "electron_browser_safe_storage")
        && addNodeLinkedBinding(env, "electron_common_features")
        && addNodeLinkedBinding(env, "electron_common_v8_util")
        && addNodeLinkedBinding(env, "electron_common_original_fs")
        && addNodeLinkedBinding(env, "electron_common_intl_collator");
}

void appendUInt32LE(std::vector<unsigned char>* out, unsigned value)
{
    out->push_back(static_cast<unsigned char>(value & 0xff));
    out->push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out->push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    out->push_back(static_cast<unsigned char>((value >> 24) & 0xff));
}

void appendPickleString(std::vector<unsigned char>* out, const std::string& value)
{
    unsigned padding = (4 - (static_cast<unsigned>(value.size()) % 4)) % 4;
    unsigned payloadSize = 4 + static_cast<unsigned>(value.size()) + padding;
    appendUInt32LE(out, payloadSize);
    appendUInt32LE(out, static_cast<unsigned>(value.size()));
    out->insert(out->end(), value.begin(), value.end());
    out->insert(out->end(), padding, 0);
}

bool writeElectronAsarSmokeArchive(std::string* asarPath)
{
    const char first[] = "asar hello\n";
    const char second[] = "nested";
    char pathBuffer[256];
    snprintf(pathBuffer, sizeof(pathBuffer), "/tmp/miniblink-asar-smoke-%d.asar", static_cast<int>(getpid()));
    *asarPath = pathBuffer;

    char headerBuffer[512];
    snprintf(headerBuffer, sizeof(headerBuffer),
        "{\"files\":{\"hello.txt\":{\"size\":%zu,\"offset\":\"0\"},\"dir\":{\"files\":{\"nested.txt\":{\"size\":%zu,\"offset\":\"%zu\"}}}}}",
        strlen(first), strlen(second), strlen(first));

    std::vector<unsigned char> headerPickle;
    appendPickleString(&headerPickle, headerBuffer);

    std::vector<unsigned char> archive;
    appendUInt32LE(&archive, 4);
    appendUInt32LE(&archive, static_cast<unsigned>(headerPickle.size()));
    archive.insert(archive.end(), headerPickle.begin(), headerPickle.end());
    archive.insert(archive.end(), first, first + strlen(first));
    archive.insert(archive.end(), second, second + strlen(second));

    FILE* file = fopen(asarPath->c_str(), "wb");
    if (!file) {
        fprintf(stderr, "failed to create %s\n", asarPath->c_str());
        return false;
    }
    bool ok = fwrite(archive.data(), 1, archive.size(), file) == archive.size();
    if (fclose(file) != 0)
        ok = false;
    if (!ok) {
        fprintf(stderr, "failed to write %s\n", asarPath->c_str());
        unlink(asarPath->c_str());
    }
    return ok;
}

bool runElectronBaseApiSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);
    setenv("MINIBLINK_SAFE_STORAGE_TEST_KEY", "1", 1);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addElectronBaseApiBindings(setup->env());
        if (ok) {
            const char scriptSource[] =
                "const { createRequire } = require('module');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "const commandLine = localRequire('../electron/lib/browser/api/command-line');"
                "const safeStorage = localRequire('../electron/lib/browser/api/safe-storage');"
                "const features = localRequire('../electron/lib/common/api/features');"
                "const v8UtilBinding = process._linkedBinding('electron_common_v8_util');"
                "const intl = localRequire('../electron/lib/common/api/intl-collator');"
                "commandLine.appendSwitch('mb-commandline-smoke', 'value');"
                "if (!commandLine.hasSwitch('mb-commandline-smoke')) throw new Error('commandLine hasSwitch');"
                "if (commandLine.getSwitchValue('mb-commandline-smoke') !== 'value') throw new Error('commandLine getSwitchValue');"
                "commandLine.appendArgument('mb-commandline-arg');"
                "if (!safeStorage.isEncryptionAvailable()) throw new Error('safeStorage unavailable');"
                "const plaintext = 'miniblink-safe-storage-smoke';"
                "const ciphertext = safeStorage.encryptString(plaintext);"
                "if (!Buffer.isBuffer(ciphertext) || ciphertext.length <= plaintext.length) throw new Error('safeStorage ciphertext');"
                "const prefix = ciphertext.subarray(0, 3).toString('utf8');"
                "if (prefix !== 'v10' && prefix !== 'v11') throw new Error('safeStorage prefix ' + prefix);"
                "if (ciphertext.includes(Buffer.from(plaintext))) throw new Error('safeStorage leaked plaintext');"
                "if (safeStorage.decryptString(ciphertext) !== plaintext) throw new Error('safeStorage decrypt');"
                "const target = {};"
                "v8UtilBinding.setHiddenValue(target, 'smoke', 42);"
                "if (v8UtilBinding.getHiddenValue(target, 'smoke') !== 42) throw new Error('v8Util getHiddenValue');"
                "v8UtilBinding.deleteHiddenValue(target, 'smoke');"
                "if (v8UtilBinding.getHiddenValue(target, 'smoke') !== undefined) throw new Error('v8Util deleteHiddenValue');"
                "if (typeof v8UtilBinding.takeHeapSnapshot !== 'function') throw new Error('v8Util takeHeapSnapshot');"
                "if (features.isDesktopCapturerEnabled !== false) throw new Error('features desktopCapturer');"
                "if (typeof features.isViewApiEnabled !== 'function') throw new Error('features isViewApiEnabled export');"
                "if (features.isViewApiEnabled() !== false) throw new Error('features isViewApiEnabled value');"
                "const collator = new intl.Collator(['en-US'], {});"
                "if (collator.compare('a', 'b') >= 0) throw new Error('intl compare');"
                "const originalFs = process._linkedBinding('electron_common_original_fs');"
                "if (!originalFs || typeof originalFs.readFileSync !== 'function') throw new Error('original-fs');"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok)
            uv_run(setup->event_loop(), UV_RUN_NOWAIT);
        if (ok)
            platform->DrainTasks(isolate);
    }

    setup.reset();
    node::TearDownOncePerProcess();
    unsetenv("MINIBLINK_SAFE_STORAGE_TEST_KEY");

    if (!ok)
        return false;

    printf("PASS electron-base-api-smoke\n");
    return true;
}

bool runElectronAsarSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    std::string asarPath;
    if (!writeElectronAsarSmokeArchive(&asarPath)) {
        setup.reset();
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_common_asar");
        if (ok) {
            std::string scriptSource = std::string(R"JS(
const fs = require('fs');
const path = require('path');
const asarPath = ")JS") + asarPath + R"JS(";
const first = 'asar hello\n';
const second = 'nested';
const firstSize = 11;
const secondSize = 6;

const binding = process._linkedBinding('electron_common_asar');
if (typeof binding.Archive !== 'function') throw new Error('asar Archive export');
if (typeof binding.initAsarSupport !== 'function') throw new Error('asar initAsarSupport export');

const archive = new binding.Archive();
if (!archive.init(asarPath)) throw new Error('archive init');
const rootEntries = archive.readdir('');
if (!Array.isArray(rootEntries) || !rootEntries.includes('hello.txt') || !rootEntries.includes('dir')) {
  throw new Error('archive readdir root');
}
const info = archive.getFileInfo('hello.txt');
if (!info || info.size !== firstSize || info.unpacked !== false) throw new Error('archive getFileInfo');
const dirStat = archive.stat('dir');
if (!dirStat || dirStat.isDirectory !== true || dirStat.isFile !== false) throw new Error('archive stat dir');
const nestedInfo = archive.getFileInfo('dir/nested.txt');
if (!nestedInfo || nestedInfo.size !== secondSize) throw new Error('archive nested getFileInfo');
const copied = archive.copyFileOut('hello.txt');
if (!copied || fs.readFileSync(copied, 'utf8') !== first) throw new Error('archive copyFileOut');
archive.destroy();
process.noDeprecation = true;
binding.initAsarSupport(process, require);
const asarFile = path.join(asarPath, 'hello.txt');
if (fs.readFileSync(asarFile, 'utf8') !== first) throw new Error('fs readFileSync asar');
if (!fs.statSync(asarFile).isFile()) throw new Error('fs statSync asar file');
const nestedFile = path.join(asarPath, 'dir', 'nested.txt');
if (fs.readFileSync(nestedFile, 'utf8') !== second) throw new Error('fs nested readFileSync asar');
const dirEntries = fs.readdirSync(path.join(asarPath, 'dir'));
if (!Array.isArray(dirEntries) || !dirEntries.includes('nested.txt')) throw new Error('fs readdirSync asar');
fs.unlinkSync(asarPath);
true;
)JS";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource.c_str());
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok)
            uv_run(setup->event_loop(), UV_RUN_NOWAIT);
        if (ok)
            platform->DrainTasks(isolate);
    }

    setup.reset();
    node::TearDownOncePerProcess();
    unlink(asarPath.c_str());

    if (!ok)
        return false;

    printf("PASS electron-asar-smoke\n");
    return true;
}

bool runNodeBootstrapSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    if (hasArg(argc, argv, "--electron-node-bootstrap-no-short-builtin-calls"))
        args.push_back("--no-short-builtin-calls");
    const char v8FlagPrefix[] = "--electron-node-v8-flag=";
    const size_t v8FlagPrefixLength = strlen(v8FlagPrefix);
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && strncmp(argv[i], v8FlagPrefix, v8FlagPrefixLength) == 0)
            args.push_back(argv[i] + v8FlagPrefixLength);
    }
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        fprintf(stderr, "node InitializeOncePerProcess failed\n");
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    uint64_t envFlags = node::EnvironmentFlags::kDefaultFlags;
    if (hasArg(argc, argv, "--electron-node-bootstrap-no-browser-globals"))
        envFlags |= node::EnvironmentFlags::kNoBrowserGlobals;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs, static_cast<node::EnvironmentFlags::Flags>(envFlags));
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_app")
            && addNodeLinkedBinding(setup->env(), "electron_browser_native_theme")
            && addNodeLinkedBinding(setup->env(), "electron_browser_powermonitor")
            && addNodeLinkedBinding(setup->env(), "electron_browser_global_shortcut")
            && addNodeLinkedBinding(setup->env(), "electron_browser_power_save_blocker")
            && addNodeLinkedBinding(setup->env(), "electron_common_screen")
            && addNodeLinkedBinding(setup->env(), "electron_browser_menu")
            && addNodeLinkedBinding(setup->env(), "electron_common_nativeImage")
            && addNodeLinkedBinding(setup->env(), "electron_common_clipboard")
            && addNodeLinkedBinding(setup->env(), "electron_common_shell")
            && addNodeLinkedBinding(setup->env(), "electron_browser_dialog")
            && addNodeLinkedBinding(setup->env(), "electron_browser_tray")
            && addNodeLinkedBinding(setup->env(), "electron_browser_protocol")
            && addNodeLinkedBinding(setup->env(), "electron_browser_commandline")
            && addNodeLinkedBinding(setup->env(), "electron_browser_safe_storage")
            && addNodeLinkedBinding(setup->env(), "electron_browser_session")
            && addNodeLinkedBinding(setup->env(), "electron_browser_webrequest")
            && addNodeLinkedBinding(setup->env(), "electron_browser_downloaditem")
            && addNodeLinkedBinding(setup->env(), "electron_browser_message_port")
            && addNodeLinkedBinding(setup->env(), "electron_browser_web_contents")
            && addNodeLinkedBinding(setup->env(), "electron_browser_browserwindow")
            && addNodeLinkedBinding(setup->env(), "electron_common_features")
            && addNodeLinkedBinding(setup->env(), "electron_common_v8_util")
            && addNodeLinkedBinding(setup->env(), "electron_common_original_fs")
            && addNodeLinkedBinding(setup->env(), "electron_common_intl_collator")
            && addNodeLinkedBinding(setup->env(), "electron_common_asar")
            && addNodeLinkedBinding(setup->env(), "electron_common_content_tracing");

        if (ok) {
            const char scriptSource[] =
                "const appBinding = process._linkedBinding('electron_browser_app');"
                "const theme = process._linkedBinding('electron_browser_native_theme');"
                "const powerMonitor = process._linkedBinding('electron_browser_powermonitor');"
                "const globalShortcut = process._linkedBinding('electron_browser_global_shortcut');"
                "const powerSaveBlocker = process._linkedBinding('electron_browser_power_save_blocker');"
                "const screen = process._linkedBinding('electron_common_screen');"
                "const menu = process._linkedBinding('electron_browser_menu');"
                "const nativeImage = process._linkedBinding('electron_common_nativeImage');"
                "const clipboard = process._linkedBinding('electron_common_clipboard');"
                "const shellBinding = process._linkedBinding('electron_common_shell');"
                "const dialog = process._linkedBinding('electron_browser_dialog');"
                "const tray = process._linkedBinding('electron_browser_tray');"
                "const protocol = process._linkedBinding('electron_browser_protocol');"
                "const commandLine = process._linkedBinding('electron_browser_commandline');"
                "const safeStorage = process._linkedBinding('electron_browser_safe_storage');"
                "const session = process._linkedBinding('electron_browser_session');"
                "const webRequest = process._linkedBinding('electron_browser_webrequest');"
                "const downloadItem = process._linkedBinding('electron_browser_downloaditem');"
                "const messagePort = process._linkedBinding('electron_browser_message_port');"
                "const webContents = process._linkedBinding('electron_browser_web_contents');"
                "const browserWindow = process._linkedBinding('electron_browser_browserwindow');"
                "const features = process._linkedBinding('electron_common_features');"
                "const v8Util = process._linkedBinding('electron_common_v8_util');"
                "const originalFs = process._linkedBinding('electron_common_original_fs');"
                "const intlCollator = process._linkedBinding('electron_common_intl_collator');"
                "const asar = process._linkedBinding('electron_common_asar');"
                "const contentTracing = process._linkedBinding('electron_common_content_tracing');"
                "if (typeof appBinding.App !== 'function') throw new Error('app');"
                "if (typeof theme.shouldUseDarkColors !== 'function') throw new Error('nativeTheme');"
                "if (typeof powerMonitor.ApiPowerMonitor !== 'function') throw new Error('powerMonitor');"
                "if (typeof globalShortcut.register !== 'function') throw new Error('globalShortcut');"
                "if (typeof powerSaveBlocker.setExecutionState !== 'function') throw new Error('powerSaveBlocker');"
                "if (typeof screen.Screen !== 'function') throw new Error('screen');"
                "if (typeof menu.Menu !== 'function') throw new Error('menu');"
                "if (typeof nativeImage.NativeImage !== 'function') throw new Error('nativeImage');"
                "if (typeof clipboard.Clipboard !== 'function') throw new Error('clipboard');"
                "if (!shellBinding.Shell || typeof shellBinding.Shell.openPath !== 'function') throw new Error('shell');"
                "if (typeof dialog.Dialog !== 'function') throw new Error('dialog');"
                "if (typeof tray.Tray !== 'function') throw new Error('tray');"
                "if (typeof protocol.Protocol !== 'function') throw new Error('protocol');"
                "if (typeof commandLine.ApiCommandLine !== 'function') throw new Error('commandLine');"
                "if (typeof safeStorage.encryptString !== 'function') throw new Error('safeStorage');"
                "if (typeof session.Session !== 'function') throw new Error('session');"
                "if (typeof webRequest.WebRequest !== 'function') throw new Error('webRequest');"
                "if (typeof downloadItem.DownloadItem !== 'function') throw new Error('downloadItem');"
                "if (typeof messagePort.createPair !== 'function') throw new Error('messagePort');"
                "if (typeof webContents.WebContents !== 'function') throw new Error('webContents');"
                "if (typeof browserWindow.BrowserWindow !== 'function') throw new Error('BrowserWindow');"
                "if (typeof features.isViewApiEnabled !== 'function') throw new Error('features');"
                "if (typeof v8Util.getHiddenValue !== 'function') throw new Error('v8Util');"
                "if (!originalFs || typeof originalFs.readFileSync !== 'function') throw new Error('originalFs');"
                "if (typeof intlCollator.IntlCollator !== 'function') throw new Error('intlCollator');"
                "if (typeof asar.Archive !== 'function') throw new Error('asar Archive');"
                "if (typeof asar.initAsarSupport !== 'function') throw new Error('asar initAsarSupport');"
                "if (typeof contentTracing.startRecording !== 'function') throw new Error('contentTracing start');"
                "if (typeof contentTracing.stopRecording !== 'function') throw new Error('contentTracing stop');"
                "if (typeof contentTracing.getTraceBufferUsage !== 'function') throw new Error('contentTracing buffer');"
                "if (typeof contentTracing.recordInstantEvent !== 'function') throw new Error('contentTracing instant');"
                "contentTracing.startRecording('electron,miniblink', 'record-continuously');"
                "contentTracing.recordInstantEvent('node-bootstrap-native-trace', '{\"ok\":true}');"
                "const nativeTraceUsage = contentTracing.getTraceBufferUsage();"
                "if (!nativeTraceUsage || nativeTraceUsage.eventCount < 1) throw new Error('contentTracing usage');"
                "const nativeTrace = JSON.parse(contentTracing.stopRecording());"
                "if (!Array.isArray(nativeTrace.traceEvents)) throw new Error('contentTracing traceEvents');"
                "if (!nativeTrace.traceEvents.some((event) => { if (event.name !== 'node-bootstrap-native-trace' || !event.args || typeof event.args.data !== 'string') return false; try { return JSON.parse(event.args.data).ok === true; } catch (error) { return false; } })) throw new Error('contentTracing native event');"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok)
            uv_run(setup->event_loop(), UV_RUN_NOWAIT);
        if (ok)
            platform->DrainTasks(isolate);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-node-bootstrap-smoke\n");
    return true;
}

bool runNativeThemeAppearanceSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    if (!electronMacNativeThemeSetAppearanceForTesting(false)) {
        fprintf(stderr, "failed to force light appearance\n");
        return false;
    }

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        electronMacNativeThemeResetAppearanceForTesting();
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        electronMacNativeThemeResetAppearanceForTesting();
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        electronMacNativeThemeResetAppearanceForTesting();
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_native_theme");
        if (ok) {
            const char scriptSource[] =
                "delete process.env.MINIBLINK_NATIVE_THEME;"
                "delete process.env.MINIBLINK_SYSTEM_DARK_MODE;"
                "delete process.env.MINIBLINK_HIGH_CONTRAST;"
                "const { createRequire } = require('module');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "const nativeTheme = localRequire('../electron/lib/browser/api/native-theme');"
                "if (nativeTheme.themeSource !== 'system') throw new Error('bad themeSource');"
                "if (nativeTheme.shouldUseDarkColors !== false) throw new Error('expected light start');"
                "globalThis.__nativeTheme = nativeTheme;"
                "globalThis.__nativeThemeEvents = [];"
                "nativeTheme.on('updated', function() {"
                "  globalThis.__nativeThemeEvents.push(nativeTheme.shouldUseDarkColors ? 'dark' : 'light');"
                "});"
                "if (!nativeTheme._nativeWatcher) throw new Error('native watcher not active');"
                "if (nativeTheme._watchTimer) throw new Error('polling watcher should not be active');"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok) {
            ok = electronMacNativeThemeSetAppearanceForTesting(true);
            drainNodeLoop(setup.get(), platform, isolate, 8);
        }
        if (ok)
            ok = runV8Script(context, "globalThis.__nativeThemeEvents.join(',') === 'dark';");

        if (ok) {
            ok = electronMacNativeThemeSetAppearanceForTesting(false);
            drainNodeLoop(setup.get(), platform, isolate, 8);
        }
        if (ok)
            ok = runV8Script(context, "globalThis.__nativeThemeEvents.join(',') === 'dark,light';");

        runV8Script(context,
            "if (globalThis.__nativeTheme) {"
            "  globalThis.__nativeTheme.removeAllListeners('updated');"
            "  if (globalThis.__nativeTheme._nativeWatcher) throw new Error('native watcher leak');"
            "}"
            "true;");
        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    electronMacNativeThemeResetAppearanceForTesting();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-native-theme-appearance-smoke\n");
    return true;
}

void dispatchPendingMessages(int iterations)
{
    for (int i = 0; i < iterations; ++i) {
        MSG msg = {};
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
        else
            usleep(10000);
    }
}

bool runGlobalShortcutDispatchSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_global_shortcut");
        if (ok) {
            const char scriptSource[] =
                "const { createRequire } = require('module');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "globalThis.__globalShortcut = localRequire('../electron/lib/browser/api/global-shortcut');"
                "globalThis.__globalShortcutNative = process._linkedBinding('electron_browser_global_shortcut');"
                "globalThis.__globalShortcutCount = 0;"
                "if (!globalThis.__globalShortcut.register('Control+Shift+G', function() { globalThis.__globalShortcutCount++; }))"
                "  throw new Error('register failed');"
                "if (!globalThis.__globalShortcut.isRegistered('Ctrl+Shift+G'))"
                "  throw new Error('isRegistered failed');"
                "if (!globalThis.__globalShortcutNative._dispatchForTesting('Control+Shift+G'))"
                "  throw new Error('dispatch failed');"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok) {
            dispatchPendingMessages(8);
            drainNodeLoop(setup.get(), platform, isolate, 8);
            ok = runV8Script(context,
                "if (globalThis.__globalShortcutCount !== 1)"
                "  throw new Error('hotkey callback count ' + globalThis.__globalShortcutCount);"
                "globalThis.__globalShortcut.unregister('Control+Shift+G');"
                "!globalThis.__globalShortcut.isRegistered('Control+Shift+G')"
                "  && !globalThis.__globalShortcutNative._dispatchForTesting('Control+Shift+G');");
        }

        runV8Script(context, "if (globalThis.__globalShortcut) globalThis.__globalShortcut.unregisterAll(); true;");
        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-global-shortcut-dispatch-smoke\n");
    return true;
}

bool runMenuApiSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_menu")
            && addNodeLinkedBinding(setup->env(), "electron_browser_app");
        if (ok) {
            const char scriptSource[] =
                "const Module = require('module');"
                "const { EventEmitter } = require('events');"
                "const { createRequire } = require('module');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "const originalLoad = Module._load;"
                "const appBinding = process._linkedBinding('electron_browser_app');"
                "const app = new appBinding.App();"
                "Object.setPrototypeOf(appBinding.App.prototype, EventEmitter.prototype);"
                "app.setName('MiniBlink Menu');"
                "const electronShim = { app };"
                "Module._load = function(request, parent, isMain) {"
                "  if (request === 'electron') return electronShim;"
                "  return originalLoad.call(this, request, parent, isMain);"
                "};"
                "const menuPath = localRequire.resolve('../electron/lib/browser/api/menu');"
                "const menuItemPath = localRequire.resolve('../electron/lib/browser/api/menu-item');"
                "const rolesPath = localRequire.resolve('../electron/lib/browser/api/menu-item-roles');"
                "const moduleCache = require.cache || {};"
                "delete moduleCache[menuPath];"
                "delete moduleCache[menuItemPath];"
                "delete moduleCache[rolesPath];"
                "globalThis.__restoreMenuSmoke = function() {"
                "  Module._load = originalLoad;"
                "};"
                "const MenuItem = localRequire('../electron/lib/browser/api/menu-item');"
                "electronShim.MenuItem = MenuItem;"
                "const Menu = localRequire('../electron/lib/browser/api/menu');"
                "electronShim.Menu = Menu;"
                "globalThis.__menuSmoke = { Menu, MenuItem, app, events: [] };"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok) {
            const char verifyScript[] =
                "const { Menu, MenuItem, app, events } = globalThis.__menuSmoke;"
                "if (typeof Menu !== 'function') throw new Error('Menu export');"
                "if (typeof MenuItem !== 'function') throw new Error('MenuItem export');"
                "if (typeof Menu.sendActionToFirstResponder !== 'function') throw new Error('sendActionToFirstResponder');"
                "if (Menu.sendActionToFirstResponder('miniblinkMissingSelector:') !== false) throw new Error('selector result');"
                "const closeItem = new MenuItem({ role: 'close' });"
                "if (closeItem.label !== 'Close Window') throw new Error('close label ' + closeItem.label);"
                "if (closeItem.getDefaultRoleAccelerator() !== 'CommandOrControl+W') throw new Error('close accelerator');"
                "const quitItem = new MenuItem({ role: 'quit' });"
                "if (quitItem.label !== 'Quit MiniBlink Menu') throw new Error('quit label ' + quitItem.label);"
                "if (quitItem.getDefaultRoleAccelerator() !== 'CommandOrControl+Q') throw new Error('quit accelerator');"
                "let appQuitCount = 0;"
                "app.quit = function() { appQuitCount++; };"
                "quitItem.click({}, null, null);"
                "if (appQuitCount !== 0) throw new Error('darwin role should not execute in JS');"
                "const menu = Menu.buildFromTemplate(["
                "  { label: 'Open', click: function(item, focusedWindow, event) {"
                "      events.push('open:' + item.label + ':' + (event && event.sender ? 'sender' : 'missing'));"
                "    } },"
                "  { type: 'checkbox', label: 'Check', checked: false, click: function(item) { events.push('check:' + item.checked); } },"
                "  { label: 'Parent', submenu: [ { label: 'Child', click: function(item) { events.push('child:' + item.label); } } ] }"
                "]);"
                "if (menu.getItemCount() !== 3) throw new Error('item count ' + menu.getItemCount());"
                "if (!Array.isArray(menu.items) || menu.items.length !== 3) throw new Error('items length');"
                "if (menu.items[2].type !== 'submenu' || menu.items[2].submenu.getItemCount() !== 1) throw new Error('submenu');"
                "if (!menu._dispatchCommandForTesting(0)) throw new Error('dispatch open');"
                "if (!menu._dispatchCommandForTesting(1)) throw new Error('dispatch check');"
                "if (events.join(',') !== 'open:Open:sender,check:true') throw new Error('events ' + events.join(','));"
                "if (menu.items[1].checked !== true) throw new Error('checkbox flag');"
                "const appMenu = Menu.buildFromTemplate([{ label: 'App' }]);"
                "Menu.setApplicationMenu(appMenu);"
                "if (Menu.getApplicationMenu() !== appMenu) throw new Error('app menu set');"
                "Menu.setApplicationMenu(null);"
                "if (Menu.getApplicationMenu() !== null) throw new Error('app menu clear');"
                "menu.clear();"
                "if (menu.getItemCount() !== 0 || menu.items.length !== 0) throw new Error('clear failed');"
                "globalThis.__restoreMenuSmoke();"
                "true;";
            ok = runV8Script(context, verifyScript);
        }

        runV8Script(context, "if (globalThis.__restoreMenuSmoke) globalThis.__restoreMenuSmoke(); true;");
        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-menu-api-smoke\n");
    return true;
}

bool runNativeImageApiSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_common_nativeImage");
        if (ok) {
            const char scriptSource[] =
                "const { createRequire } = require('module');"
                "const fs = require('fs');"
                "const os = require('os');"
                "const path = require('path');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "const { NativeImage } = localRequire('../electron/lib/common/api/native-image');"
                "if (typeof NativeImage !== 'function') throw new Error('NativeImage export');"
                "if (typeof NativeImage.createEmpty !== 'function') throw new Error('createEmpty export');"
                "if (typeof NativeImage.createFromBuffer !== 'function') throw new Error('createFromBuffer export');"
                "if (typeof NativeImage.createFromPath !== 'function') throw new Error('createFromPath export');"
                "const empty = NativeImage.createEmpty();"
                "if (!empty.isEmpty()) throw new Error('empty image should be empty');"
                "const emptySize = empty.getSize();"
                "if (emptySize.width !== 0 || emptySize.height !== 0) throw new Error('empty size');"
                "const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==', 'base64');"
                "const image = NativeImage.createFromBuffer(png);"
                "if (image.isEmpty()) throw new Error('buffer decode failed');"
                "const size = image.getSize();"
                "if (size.width !== 1 || size.height !== 1) throw new Error('bad size ' + JSON.stringify(size));"
                "const outPng = image.toPNG();"
                "const pngInfo = { isBuffer: Buffer.isBuffer(outPng), type: outPng && outPng.constructor && outPng.constructor.name, length: outPng && outPng.length, first: outPng && Array.prototype.slice.call(outPng, 0, 8) };"
                "if (!Buffer.isBuffer(outPng) || outPng.length <= 8 || outPng.readUInt32BE(0) !== 0x89504e47) throw new Error('png encode ' + JSON.stringify(pngInfo));"
                "const outJpeg = image.toJPEG();"
                "if (!Buffer.isBuffer(outJpeg) || outJpeg.length <= 4 || outJpeg[0] !== 0xff || outJpeg[1] !== 0xd8) throw new Error('jpeg encode');"
                "const bitmap = image.toBitmap();"
                "if (!Buffer.isBuffer(bitmap) || bitmap.length !== 4) throw new Error('bitmap length ' + (bitmap && bitmap.length));"
                "if (typeof image.toDataURL() !== 'string' || !image.toDataURL().startsWith('data:image/jpeg;base64,')) throw new Error('data url');"
                "const filePath = path.join(os.tmpdir(), 'miniblink-native-image-smoke-' + process.pid + '.png');"
                "fs.writeFileSync(filePath, png);"
                "try {"
                "  const fromPath = NativeImage.createFromPath(filePath);"
                "  if (!fromPath || fromPath.isEmpty()) throw new Error('path decode failed');"
                "  const pathSize = fromPath.getSize();"
                "  if (pathSize.width !== 1 || pathSize.height !== 1) throw new Error('path size');"
                "} finally {"
                "  try { fs.unlinkSync(filePath); } catch (e) {}"
                "}"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-native-image-api-smoke\n");
    return true;
}

bool runClipboardApiSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_common_clipboard");
        if (ok) {
            const char scriptSource[] =
                "const { createRequire } = require('module');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "const clipboard = localRequire('../electron/lib/common/api/clipboard');"
                "if (!clipboard || typeof clipboard.writeText !== 'function') throw new Error('clipboard export');"
                "if (typeof clipboard.readText !== 'function') throw new Error('readText export');"
                "if (typeof clipboard.clear !== 'function') throw new Error('clear export');"
                "if (typeof clipboard.writeBuffer !== 'function') throw new Error('writeBuffer export');"
                "if (typeof clipboard.readBuffer !== 'function') throw new Error('readBuffer export');"
                "clipboard.clear();"
                "clipboard.writeText('MiniBlink clipboard utf8');"
                "if (clipboard.readText() !== 'MiniBlink clipboard utf8') throw new Error('text roundtrip');"
                "const unicodeText = 'MiniBlink unicode ' + String.fromCharCode(0x4e2d);"
                "clipboard.writeText(unicodeText);"
                "if (clipboard.readText() !== unicodeText) throw new Error('unicode roundtrip ' + clipboard.readText());"
                "const payload = Buffer.from('buffer-payload');"
                "clipboard.writeBuffer('application/x-miniblink-clipboard-smoke', payload);"
                "const buffer = clipboard.readBuffer('application/x-miniblink-clipboard-smoke');"
                "if (!Buffer.isBuffer(buffer) || buffer.toString() !== 'buffer-payload') throw new Error('buffer roundtrip');"
                "clipboard.writeHTML('<b>Clip</b>');"
                "if (clipboard.readHTML() !== '<b>Clip</b>') throw new Error('html roundtrip ' + clipboard.readHTML());"
                "clipboard.writeRTF('{\\\\rtf1 Clip}');"
                "if (clipboard.readRTF().indexOf('Clip') < 0) throw new Error('rtf roundtrip');"
                "clipboard.clear();"
                "if (clipboard.readText() !== '') throw new Error('clear text');"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-clipboard-api-smoke\n");
    return true;
}

bool runShellApiSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_common_shell");
        if (ok) {
            const char scriptSource[] =
                "const { createRequire } = require('module');"
                "const fs = require('fs');"
                "const path = require('path');"
                "const os = require('os');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "const shellModule = localRequire('../electron/lib/common/api/shell');"
                "const shell = shellModule.shell;"
                "const root = path.join(os.tmpdir(), 'miniblink-electron-shell-smoke-' + process.pid);"
                "fs.rmSync(root, { recursive: true, force: true });"
                "fs.mkdirSync(root, { recursive: true });"
                "const openPath = path.join(root, 'open-path.txt');"
                "const logPath = path.join(root, 'shell-execute.log');"
                "fs.writeFileSync(openPath, 'shell open ok\\n');"
                "process.env.MINIBLINK_SHELL_EXECUTE_LOG = logPath;"
                "process.env.MINIBLINK_SUPPRESS_BEEP = '1';"
                "globalThis.__electronShellSmokeDone = false;"
                "globalThis.__electronShellSmokeError = '';"
                "(async function() {"
                "  try {"
                "    if (shellModule.Shell !== shell) throw new Error('bad shell exports');"
                "    ['openExternal', 'openPath', 'showItemInFolder', 'moveItemToTrash', 'trashItem', 'beep'].forEach(function(name) {"
                "      if (typeof shell[name] !== 'function') throw new Error('missing shell.' + name);"
                "    });"
                "    const openResult = await shell.openPath(openPath);"
                "    if (openResult !== '') throw new Error('openPath result ' + openResult);"
                "    if (shell.openExternal('https://example.com/', { activate: false }) !== true) throw new Error('openExternal failed');"
                "    shell.showItemInFolder(openPath);"
                "    shell.beep();"
                "    const logText = fs.readFileSync(logPath, 'utf8');"
                "    if (!logText.includes('/usr/bin/open\\t' + openPath)) throw new Error('missing openPath log ' + logText);"
                "    if (!logText.includes('/usr/bin/open\\thttps://example.com/')) throw new Error('missing openExternal log ' + logText);"
                "    if (!logText.includes('/usr/bin/open\\t-R\\t' + openPath)) throw new Error('missing showItemInFolder log ' + logText);"
                "    const trashName = 'trash-' + process.pid + '-' + Date.now() + '.txt';"
                "    const trashPath = path.join(root, trashName);"
                "    const expectedTrashPath = path.join(os.homedir(), '.Trash', trashName);"
                "    fs.rmSync(expectedTrashPath, { force: true });"
                "    fs.writeFileSync(trashPath, 'trash ok\\n');"
                "    if (shell.moveItemToTrash(trashPath) !== true) throw new Error('moveItemToTrash failed');"
                "    if (fs.existsSync(trashPath) || !fs.existsSync(expectedTrashPath)) throw new Error('legacy trash path mismatch');"
                "    fs.rmSync(expectedTrashPath, { force: true });"
                "    const promiseTrashName = 'trash-promise-' + process.pid + '-' + Date.now() + '.txt';"
                "    const promiseTrashPath = path.join(root, promiseTrashName);"
                "    const expectedPromiseTrashPath = path.join(os.homedir(), '.Trash', promiseTrashName);"
                "    fs.rmSync(expectedPromiseTrashPath, { force: true });"
                "    fs.writeFileSync(promiseTrashPath, 'trash promise ok\\n');"
                "    await shell.trashItem(promiseTrashPath);"
                "    if (fs.existsSync(promiseTrashPath) || !fs.existsSync(expectedPromiseTrashPath)) throw new Error('trashItem path mismatch');"
                "    fs.rmSync(expectedPromiseTrashPath, { force: true });"
                "    let rejected = false;"
                "    try { await shell.trashItem(path.join(root, 'missing.txt')); }"
                "    catch (error) { rejected = /Failed to move item to trash/.test(error && error.message ? error.message : String(error)); }"
                "    if (!rejected) throw new Error('trashItem missing path did not reject');"
                "  } catch (error) {"
                "    globalThis.__electronShellSmokeError = error && error.stack ? error.stack : String(error);"
                "  } finally {"
                "    delete process.env.MINIBLINK_SHELL_EXECUTE_LOG;"
                "    delete process.env.MINIBLINK_SUPPRESS_BEEP;"
                "    fs.rmSync(root, { recursive: true, force: true });"
                "    globalThis.__electronShellSmokeDone = true;"
                "  }"
                "})();"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok) {
            for (int i = 0; i < 200; ++i) {
                drainNodeLoop(setup.get(), platform, isolate, 4);
                if (runV8Script(context, "globalThis.__electronShellSmokeDone === true;"))
                    break;
                usleep(10000);
            }

            const char verifyScript[] =
                "if (!globalThis.__electronShellSmokeDone) throw new Error('shell smoke did not finish');"
                "if (globalThis.__electronShellSmokeError) throw new Error(globalThis.__electronShellSmokeError);"
                "true;";
            ok = runV8Script(context, verifyScript);
        }

        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-shell-api-smoke\n");
    return true;
}

bool runDialogApiSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_dialog");
        if (ok) {
            const char scriptSource[] =
                "const { createRequire } = require('module');"
                "const fs = require('fs');"
                "const path = require('path');"
                "const os = require('os');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "const dialogModule = localRequire('../electron/lib/browser/api/dialog');"
                "const dialog = dialogModule.dialog;"
                "const root = path.join(os.tmpdir(), 'miniblink-electron-dialog-smoke-' + process.pid);"
                "fs.rmSync(root, { recursive: true, force: true });"
                "fs.mkdirSync(root, { recursive: true });"
                "const openPath = path.join(root, 'open.txt');"
                "const savePath = path.join(root, 'save.txt');"
                "fs.writeFileSync(openPath, 'dialog open ok\\n');"
                "process.env.MINIBLINK_OPEN_FILE_NAME = openPath;"
                "process.env.MINIBLINK_SAVE_FILE_NAME = savePath;"
                "process.env.MINIBLINK_FOLDER_CHOOSER_PATH = root;"
                "globalThis.__electronDialogSmokeDone = false;"
                "globalThis.__electronDialogSmokeError = '';"
                "(async function() {"
                "  try {"
                "    if (!dialog || typeof dialog.showOpenDialogSync !== 'function') throw new Error('dialog export');"
                "    if (typeof dialog.showSaveDialogSync !== 'function') throw new Error('save sync export');"
                "    if (typeof dialog.showOpenDialog !== 'function') throw new Error('open async export');"
                "    if (typeof dialog.showMessageBox !== 'function') throw new Error('message async export');"
                "    if (typeof dialog.showErrorBox !== 'function') throw new Error('error box export');"
                "    const openResult = dialog.showOpenDialogSync({"
                "      defaultPath: openPath,"
                "      properties: ['openFile'],"
                "      filters: [{ name: 'Text', extensions: ['txt'] }]"
                "    });"
                "    if (!Array.isArray(openResult) || openResult.length !== 1 || openResult[0] !== openPath)"
                "      throw new Error('open sync result ' + JSON.stringify(openResult));"
                "    const saveResult = dialog.showSaveDialogSync({"
                "      defaultPath: savePath,"
                "      filters: [{ name: 'Text', extensions: ['txt'] }]"
                "    });"
                "    if (saveResult !== savePath) throw new Error('save sync result ' + saveResult);"
                "    const dirResult = dialog.showOpenDialogSync({ defaultPath: root, properties: ['openDirectory'] });"
                "    if (!Array.isArray(dirResult) || dirResult.length !== 1 || dirResult[0] !== root)"
                "      throw new Error('directory result ' + JSON.stringify(dirResult));"
                "    const syncMessage = dialog.showMessageBoxSync({"
                "      type: 'question',"
                "      buttons: ['Yes', 'No'],"
                "      title: 'Dialog smoke',"
                "      message: 'sync path'"
                "    });"
                "    if (syncMessage !== 0) throw new Error('message sync result ' + syncMessage);"
                "    const openAsync = await dialog.showOpenDialog({ defaultPath: openPath, properties: ['openFile'] });"
                "    if (!openAsync || openAsync.canceled !== false || !Array.isArray(openAsync.filePaths) || openAsync.filePaths[0] !== openPath)"
                "      throw new Error('open async result ' + JSON.stringify(openAsync));"
                "    const saveAsync = await dialog.showSaveDialog({ defaultPath: savePath });"
                "    if (!saveAsync || saveAsync.canceled !== false || saveAsync.filePath !== savePath)"
                "      throw new Error('save async result ' + JSON.stringify(saveAsync));"
                "    const messageAsync = await dialog.showMessageBox({ buttons: ['OK'], title: 'Dialog smoke', message: 'async path' });"
                "    if (!messageAsync || messageAsync.response !== 0 || messageAsync.checkboxChecked !== false)"
                "      throw new Error('message async result ' + JSON.stringify(messageAsync));"
                "    dialog.showErrorBox('Dialog smoke', 'error path');"
                "  } catch (error) {"
                "    globalThis.__electronDialogSmokeError = error && error.stack ? error.stack : String(error);"
                "  } finally {"
                "    delete process.env.MINIBLINK_OPEN_FILE_NAME;"
                "    delete process.env.MINIBLINK_SAVE_FILE_NAME;"
                "    delete process.env.MINIBLINK_FOLDER_CHOOSER_PATH;"
                "    fs.rmSync(root, { recursive: true, force: true });"
                "    globalThis.__electronDialogSmokeDone = true;"
                "  }"
                "})();"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok) {
            for (int i = 0; i < 200; ++i) {
                drainNodeLoop(setup.get(), platform, isolate, 4);
                if (runV8Script(context, "globalThis.__electronDialogSmokeDone === true;"))
                    break;
                usleep(10000);
            }

            const char verifyScript[] =
                "if (!globalThis.__electronDialogSmokeDone) throw new Error('dialog smoke did not finish');"
                "if (globalThis.__electronDialogSmokeError) throw new Error(globalThis.__electronDialogSmokeError);"
                "true;";
            ok = runV8Script(context, verifyScript);
        }

        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-dialog-api-smoke\n");
    return true;
}

bool runTrayApiSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_tray")
            && addNodeLinkedBinding(setup->env(), "electron_browser_menu")
            && addNodeLinkedBinding(setup->env(), "electron_browser_app");
        if (ok) {
            const char scriptSource[] =
                "const Module = require('module');"
                "const { EventEmitter } = require('events');"
                "const { createRequire } = require('module');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "const originalLoad = Module._load;"
                "const appBinding = process._linkedBinding('electron_browser_app');"
                "const app = new appBinding.App();"
                "Object.setPrototypeOf(appBinding.App.prototype, EventEmitter.prototype);"
                "app.setName('MiniBlink Tray');"
                "const electronShim = { app };"
                "Module._load = function(request, parent, isMain) {"
                "  if (request === 'electron') return electronShim;"
                "  return originalLoad.call(this, request, parent, isMain);"
                "};"
                "const menuPath = localRequire.resolve('../electron/lib/browser/api/menu');"
                "const menuItemPath = localRequire.resolve('../electron/lib/browser/api/menu-item');"
                "const rolesPath = localRequire.resolve('../electron/lib/browser/api/menu-item-roles');"
                "const trayPath = localRequire.resolve('../electron/lib/browser/api/tray');"
                "const moduleCache = require.cache || {};"
                "delete moduleCache[menuPath];"
                "delete moduleCache[menuItemPath];"
                "delete moduleCache[rolesPath];"
                "delete moduleCache[trayPath];"
                "globalThis.__restoreTraySmoke = function() { Module._load = originalLoad; };"
                "const MenuItem = localRequire('../electron/lib/browser/api/menu-item');"
                "electronShim.MenuItem = MenuItem;"
                "const Menu = localRequire('../electron/lib/browser/api/menu');"
                "electronShim.Menu = Menu;"
                "const trayModule = localRequire('../electron/lib/browser/api/tray');"
                "globalThis.__traySmoke = { Tray: trayModule.Tray, Menu, MenuItem, events: [] };"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok) {
            const char verifyScript[] =
                "const { Tray, Menu, events } = globalThis.__traySmoke;"
                "if (typeof Tray !== 'function') throw new Error('Tray export');"
                "const tray = new Tray('');"
                "if (typeof tray.setToolTip !== 'function') throw new Error('setToolTip');"
                "if (typeof tray.displayBalloon !== 'function') throw new Error('displayBalloon');"
                "if (typeof tray.setImage !== 'function') throw new Error('setImage');"
                "if (typeof tray.destroy !== 'function') throw new Error('destroy');"
                "if (typeof tray.setContextMenu !== 'function') throw new Error('setContextMenu');"
                "tray.setToolTip('MiniBlink Tray Smoke');"
                "tray.displayBalloon({ title: 'Tray smoke', content: 'native bridge' });"
                "tray.setImage('');"
                "const menu = Menu.buildFromTemplate([{ label: 'Open', click: function() { events.push('menu-open'); } }]);"
                "let rightClickCount = 0;"
                "tray.on('right-click', function() { rightClickCount++; });"
                "tray.setContextMenu(menu);"
                "if (tray.menu !== menu) throw new Error('context menu not retained');"
                "tray.onNativeMessage('right-click');"
                "if (rightClickCount !== 1) throw new Error('right click count ' + rightClickCount);"
                "tray.onNativeMessage('click');"
                "let clickCount = 0;"
                "tray.on('click', function() { clickCount++; });"
                "tray.onNativeMessage('click');"
                "if (clickCount !== 1) throw new Error('click count ' + clickCount);"
                "tray.destroy();"
                "globalThis.__restoreTraySmoke();"
                "true;";
            ok = runV8Script(context, verifyScript);
        }

        runV8Script(context, "if (globalThis.__restoreTraySmoke) globalThis.__restoreTraySmoke(); true;");
        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-tray-api-smoke\n");
    return true;
}

bool runProtocolApiSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_protocol");
        if (ok) {
            const char scriptSource[] =
                "const { createRequire } = require('module');"
                "process._rawDebug('protocol smoke stage: createRequire');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "globalThis.__electronProtocolSkipBlinkRegistrationForTesting = true;"
                "process._rawDebug('protocol smoke stage: before require protocol');"
                "const protocolModule = localRequire('../electron/lib/browser/api/protocol');"
                "process._rawDebug('protocol smoke stage: after require protocol');"
                "const protocol = protocolModule.protocol;"
                "const { Readable } = localRequire('stream');"
                "if (!protocol || typeof protocol.registerStringProtocol !== 'function') throw new Error('protocol export');"
                "if (typeof protocol.registerFileProtocol !== 'function') throw new Error('file export');"
                "if (typeof protocol.registerBufferProtocol !== 'function') throw new Error('buffer export');"
                "if (typeof protocol.registerHttpProtocol !== 'function') throw new Error('http export');"
                "if (typeof protocol.interceptStringProtocol !== 'function') throw new Error('intercept export');"
                "if (typeof protocol.unregisterProtocol !== 'function') throw new Error('unregister export');"
                "if (typeof protocol.registerStandardSchemes !== 'function') throw new Error('standard scheme export');"
                "if (typeof protocol.registerSchemesAsPrivileged !== 'function') throw new Error('privileged scheme export');"
                "if (typeof protocol.registerStreamProtocol !== 'function') throw new Error('stream register export');"
                "if (typeof protocol.interceptStreamProtocol !== 'function') throw new Error('stream intercept export');"
                "globalThis.__protocolSmoke = { protocol, Readable, events: [] };"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok) {
            const char verifyScript[] =
                "const { protocol, Readable, events } = globalThis.__protocolSmoke;"
                "function isHandled(scheme) {"
                "  let result = null;"
                "  protocol.isProtocolHandled(scheme, value => { result = value; });"
                "  return result;"
                "}"
                "process._rawDebug('protocol smoke stage: before standard');"
                "protocol.registerStandardSchemes(['mbstandard']);"
                "process._rawDebug('protocol smoke stage: before privileged');"
                "protocol.registerSchemesAsPrivileged([{ scheme: 'mbprivileged', privileges: { secure: true, standard: true, bypassCSP: true } }]);"
                "process._rawDebug('protocol smoke stage: before file');"
                "protocol.registerFileProtocol('mbfile', function(request, callback) {"
                "  callback({ path: '/tmp/miniblink-protocol-smoke' });"
                "}, function(error) { if (error) throw error; });"
                "if (isHandled('mbfile') !== true) throw new Error('mbfile not handled');"
                "protocol.unregisterProtocol('mbfile', function(error) { if (error) throw error; });"
                "if (isHandled('mbfile') !== false) throw new Error('mbfile still handled');"
                "protocol.registerStringProtocol('mbstring', function(request, callback) {"
                "  events.push('string:' + request.url);"
                "  callback({ data: 'ok', mimeType: 'text/plain' });"
                "}, function(error) { if (error) throw error; });"
                "if (isHandled('mbstring') !== true) throw new Error('mbstring not handled');"
                "let duplicateMessage = '';"
                "protocol.registerStringProtocol('mbstring', function() {}, function(error) {"
                "  duplicateMessage = error ? String(error.message || error) : '';"
                "});"
                "if (!duplicateMessage) throw new Error('duplicate registration should fail');"
                "protocol.unregisterProtocol('mbstring', function(error) { if (error) throw error; });"
                "if (isHandled('mbstring') !== false) throw new Error('mbstring still handled');"
                "protocol.registerBufferProtocol('mbbuffer', function(request, callback) {"
                "  callback({ data: Buffer.from('buffer-ok'), mimeType: 'text/plain' });"
                "}, function(error) { if (error) throw error; });"
                "if (isHandled('mbbuffer') !== true) throw new Error('mbbuffer not handled');"
                "protocol.unregisterProtocol('mbbuffer');"
                "if (isHandled('mbbuffer') !== false) throw new Error('mbbuffer still handled');"
                "protocol.registerHttpProtocol('mbhttp', function(request, callback) {"
                "  callback({ url: 'https://example.invalid/' });"
                "}, function(error) { if (error) throw error; });"
                "if (isHandled('mbhttp') !== true) throw new Error('mbhttp not handled');"
                "protocol.unregisterProtocol('mbhttp');"
                "if (isHandled('mbhttp') !== false) throw new Error('mbhttp still handled');"
                "protocol.registerStreamProtocol('mbstream', function(request, callback) {"
                "  callback({ data: Readable.from(['stream-ok']), mimeType: 'text/plain' });"
                "}, function(error) { if (error) throw error; });"
                "if (isHandled('mbstream') !== true) throw new Error('mbstream not handled');"
                "protocol.unregisterProtocol('mbstream');"
                "if (isHandled('mbstream') !== false) throw new Error('mbstream still handled');"
                "protocol.interceptStreamProtocol('mbstream2', function(request, callback) {"
                "  callback(Readable.from([Buffer.from('intercept-ok')]));"
                "}, function(error) { if (error) throw error; });"
                "if (isHandled('mbstream2') !== true) throw new Error('stream intercept not handled');"
                "protocol.uninterceptProtocol('mbstream2');"
                "if (isHandled('mbstream2') !== false) throw new Error('stream intercept still handled');"
                "protocol.interceptStringProtocol('mbintercept', function(request, callback) {"
                "  callback({ data: 'intercept-ok', mimeType: 'text/plain' });"
                "}, function(error) { if (error) throw error; });"
                "if (isHandled('mbintercept') !== true) throw new Error('intercept not handled');"
                "protocol.uninterceptProtocol('mbintercept');"
                "if (isHandled('mbintercept') !== false) throw new Error('intercept still handled');"
                "delete globalThis.__electronProtocolSkipBlinkRegistrationForTesting;"
                "true;";
            ok = runV8Script(context, verifyScript);
        }

        runV8Script(context, "delete globalThis.__electronProtocolSkipBlinkRegistrationForTesting; true;");
        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-protocol-api-smoke\n");
    return true;
}

bool runPowerMonitorEventSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_powermonitor");
        if (ok) {
            const char scriptSource[] =
                "const { createRequire } = require('module');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "const powerMonitor = localRequire('../electron/lib/browser/api/power-monitor');"
                "if (typeof powerMonitor.on !== 'function') throw new Error('powerMonitor.on');"
                "if (typeof powerMonitor.getSystemIdleState !== 'function') throw new Error('idle state');"
                "if (typeof powerMonitor.isOnBatteryPower !== 'function') throw new Error('battery');"
                "globalThis.__powerMonitor = powerMonitor;"
                "globalThis.__powerMonitorEvents = [];"
                "['suspend','resume','on-battery','on-ac'].forEach(function(name) {"
                "  powerMonitor.on(name, function() { globalThis.__powerMonitorEvents.push(name); });"
                "});"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok) {
            MacDispatchPowerMonitorMessageForTesting(PBT_APMSUSPEND);
            dispatchPendingMessages(4);
            MacDispatchPowerMonitorMessageForTesting(PBT_APMRESUMESUSPEND);
            dispatchPendingMessages(4);
            setenv("MINIBLINK_POWER_AC", "0", 1);
            MacDispatchPowerMonitorMessageForTesting(PBT_APMPOWERSTATUSCHANGE);
            dispatchPendingMessages(4);
            setenv("MINIBLINK_POWER_AC", "1", 1);
            MacDispatchPowerMonitorMessageForTesting(PBT_APMPOWERSTATUSCHANGE);
            dispatchPendingMessages(4);
            unsetenv("MINIBLINK_POWER_AC");
            drainNodeLoop(setup.get(), platform, isolate, 8);
        }

        if (ok) {
            const char verifyScript[] =
                "if (globalThis.__powerMonitorEvents.join(',') !== 'suspend,resume,on-battery,on-ac')"
                "  throw new Error('bad powerMonitor order ' + globalThis.__powerMonitorEvents.join(','));"
                "globalThis.__powerMonitor.removeAllListeners();"
                "true;";
            ok = runV8Script(context, verifyScript);
        }

        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-power-monitor-event-smoke\n");
    return true;
}

bool waitForPowerSaveAssertions(int systemExpected, int displayExpected)
{
    for (int i = 0; i < 100; ++i) {
        BOOL systemOn = FALSE;
        BOOL displayOn = FALSE;
        MacGetPowerSaveBlockerAssertionStateForTesting(&systemOn, &displayOn);
        if ((systemExpected < 0 || systemOn == !!systemExpected)
            && (displayExpected < 0 || displayOn == !!displayExpected))
            return true;
        usleep(10000);
    }
    BOOL systemOn = FALSE;
    BOOL displayOn = FALSE;
    MacGetPowerSaveBlockerAssertionStateForTesting(&systemOn, &displayOn);
    fprintf(stderr, "powerSaveBlocker assertion state mismatch system=%d display=%d expectedSystem=%d expectedDisplay=%d\n",
        systemOn ? 1 : 0,
        displayOn ? 1 : 0,
        systemExpected,
        displayExpected);
    return false;
}

bool runPowerSaveBlockerLifecycleSmoke(int argc, char** argv)
{
    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    SetThreadExecutionState(ES_CONTINUOUS);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_power_save_blocker");
        if (ok) {
            const char scriptSource[] =
                "const { createRequire } = require('module');"
                "const localRequire = createRequire(process.cwd() + '/mac/electron_api_smoke.js');"
                "globalThis.__powerSaveBlocker = localRequire('../electron/lib/browser/api/power-save-blocker');"
                "globalThis.__powerSaveBlockerIds = [];"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok) {
            ok = runV8Script(context,
                "globalThis.__powerSaveBlockerIds.push(globalThis.__powerSaveBlocker.start('prevent-app-suspension'));"
                "globalThis.__powerSaveBlockerIds.push(globalThis.__powerSaveBlocker.start('prevent-app-suspension'));"
                "globalThis.__powerSaveBlocker.isStarted(globalThis.__powerSaveBlockerIds[0])"
                "  && globalThis.__powerSaveBlocker.isStarted(globalThis.__powerSaveBlockerIds[1]);")
                && waitForPowerSaveAssertions(1, -1);
        }

        if (ok) {
            ok = runV8Script(context,
                "globalThis.__powerSaveBlockerIds.push(globalThis.__powerSaveBlocker.start('prevent-display-sleep'));"
                "globalThis.__powerSaveBlocker.isStarted(globalThis.__powerSaveBlockerIds[2]);")
                && waitForPowerSaveAssertions(1, 1);
        }

        if (ok) {
            ok = runV8Script(context,
                "globalThis.__powerSaveBlocker.stop(globalThis.__powerSaveBlockerIds[2]);"
                "!globalThis.__powerSaveBlocker.isStarted(globalThis.__powerSaveBlockerIds[2])"
                "  && globalThis.__powerSaveBlocker.isStarted(globalThis.__powerSaveBlockerIds[0]);")
                && waitForPowerSaveAssertions(1, -1);
        }

        if (ok) {
            ok = runV8Script(context,
                "globalThis.__powerSaveBlocker.stop(globalThis.__powerSaveBlockerIds[0]);"
                "globalThis.__powerSaveBlocker.stop(globalThis.__powerSaveBlockerIds[1]);"
                "!globalThis.__powerSaveBlocker.isStarted(globalThis.__powerSaveBlockerIds[0])"
                "  && !globalThis.__powerSaveBlocker.isStarted(globalThis.__powerSaveBlockerIds[1]);");
        }

        if (ok)
            ok = waitForPowerSaveAssertions(0, 0);

        if (ok) {
            ok = runV8Script(context,
                "globalThis.__powerSaveBlockerStressAppIds = [];"
                "globalThis.__powerSaveBlockerStressDisplayIds = [];"
                "for (let i = 0; i < 6; ++i)"
                "  globalThis.__powerSaveBlockerStressAppIds.push(globalThis.__powerSaveBlocker.start('prevent-app-suspension'));"
                "for (let i = 0; i < 4; ++i)"
                "  globalThis.__powerSaveBlockerStressDisplayIds.push(globalThis.__powerSaveBlocker.start('prevent-display-sleep'));"
                "globalThis.__powerSaveBlockerStressAppIds.every(id => globalThis.__powerSaveBlocker.isStarted(id))"
                "  && globalThis.__powerSaveBlockerStressDisplayIds.every(id => globalThis.__powerSaveBlocker.isStarted(id));")
                && waitForPowerSaveAssertions(1, 1);
        }

        if (ok) {
            ok = runV8Script(context,
                "[2, 0, 3, 1].forEach(index => globalThis.__powerSaveBlocker.stop(globalThis.__powerSaveBlockerStressDisplayIds[index]));"
                "globalThis.__powerSaveBlocker.stop(globalThis.__powerSaveBlockerStressDisplayIds[0]);"
                "globalThis.__powerSaveBlockerStressDisplayIds.every(id => !globalThis.__powerSaveBlocker.isStarted(id))"
                "  && globalThis.__powerSaveBlockerStressAppIds.every(id => globalThis.__powerSaveBlocker.isStarted(id));")
                && waitForPowerSaveAssertions(1, 0);
        }

        if (ok) {
            ok = runV8Script(context,
                "[5, 1, 3, 0, 4, 2].forEach(index => globalThis.__powerSaveBlocker.stop(globalThis.__powerSaveBlockerStressAppIds[index]));"
                "globalThis.__powerSaveBlocker.stop(globalThis.__powerSaveBlockerStressAppIds[0]);"
                "globalThis.__powerSaveBlocker.stop(999999);"
                "globalThis.__powerSaveBlockerStressAppIds.every(id => !globalThis.__powerSaveBlocker.isStarted(id));")
                && waitForPowerSaveAssertions(0, 0);
        }

        runV8Script(context,
            "if (globalThis.__powerSaveBlocker) {"
            "  for (const list of [globalThis.__powerSaveBlockerIds, globalThis.__powerSaveBlockerStressAppIds, globalThis.__powerSaveBlockerStressDisplayIds])"
            "    for (const id of list || [])"
            "      globalThis.__powerSaveBlocker.stop(id);"
            "}"
            "true;");
        drainNodeLoop(setup.get(), platform, isolate, 8);
    }

    setup.reset();
    SetThreadExecutionState(ES_CONTINUOUS);
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-power-save-blocker-lifecycle-smoke\n");
    return true;
}

bool runAppLifecycleSmoke(int argc, char** argv)
{
    if (!electronWindowListCloseAllWindowsSmokeForTesting()) {
        fprintf(stderr, "WindowList closeAllWindows cancellation smoke failed\n");
        return false;
    }

    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        fprintf(stderr, "node InitializeOncePerProcess failed\n");
        return false;
    }

    node::MultiIsolatePlatform* platform = init->platform();
    if (!platform) {
        fprintf(stderr, "node InitializeOncePerProcess did not create a V8 platform\n");
        node::TearDownOncePerProcess();
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(
        platform, &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_app");
        if (ok) {
            const char scriptSource[] =
                "const { EventEmitter } = require('events');"
                "const { App } = process._linkedBinding('electron_browser_app');"
                "Object.setPrototypeOf(App.prototype, EventEmitter.prototype);"
                "const app = new App();"
                "const events = [];"
                "app.on('before-quit', function(event) { if (!event || event.sender !== app) throw new Error('before event'); events.push('before-quit'); });"
                "app.on('window-all-closed', function(event) { if (!event || event.sender !== app) throw new Error('window event'); events.push('window-all-closed'); });"
                "app.on('quit', function(event, code) { if (!event || event.sender !== app) throw new Error('quit event'); events.push('quit:' + code); });"
                "app.quit();"
                "if (events.join(',') !== 'before-quit,window-all-closed,quit:0') throw new Error('bad quit order ' + events.join(','));"
                "const cancelApp = new App();"
                "const cancelEvents = [];"
                "let cancelNextQuit = true;"
                "let cancelPrevented = false;"
                "cancelApp.on('before-quit', function(event) {"
                "  if (!event || event.sender !== cancelApp) throw new Error('cancel before event');"
                "  cancelEvents.push('before-quit');"
                "  if (cancelNextQuit) {"
                "    cancelNextQuit = false;"
                "    event.preventDefault();"
                "    cancelPrevented = event.defaultPrevented === true;"
                "  }"
                "});"
                "cancelApp.on('window-all-closed', function() { cancelEvents.push('window-all-closed'); });"
                "cancelApp.on('quit', function(event, code) {"
                "  if (!event || event.sender !== cancelApp) throw new Error('cancel quit event');"
                "  cancelEvents.push('quit:' + code);"
                "});"
                "cancelApp.quit();"
                "if (!cancelPrevented) throw new Error('before-quit preventDefault not observed');"
                "if (cancelEvents.join(',') !== 'before-quit')"
                "  throw new Error('bad cancel order ' + cancelEvents.join(','));"
                "const retryApp = new App();"
                "const retryEvents = [];"
                "retryApp.on('before-quit', function(event) {"
                "  if (!event || event.sender !== retryApp) throw new Error('retry before event');"
                "  retryEvents.push('before-quit');"
                "});"
                "retryApp.on('window-all-closed', function() { retryEvents.push('window-all-closed'); });"
                "retryApp.on('quit', function(event, code) {"
                "  if (!event || event.sender !== retryApp) throw new Error('retry quit event');"
                "  retryEvents.push('quit:' + code);"
                "});"
                "retryApp.quit();"
                "if (retryEvents.join(',') !== 'before-quit,window-all-closed,quit:0')"
                "  throw new Error('bad retry order ' + retryEvents.join(','));"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok)
            uv_run(setup->event_loop(), UV_RUN_NOWAIT);
        if (ok)
            platform->DrainTasks(isolate);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-app-lifecycle-smoke\n");
    return true;
}

bool runAppExitChildSmoke(int argc, char** argv)
{
    if (argc < 1 || !argv[0])
        return false;

    pid_t pid = fork();
    if (pid == 0) {
        execl(argv[0], argv[0], "--electron-app-exit-child", nullptr);
        _exit(127);
    }
    if (pid < 0) {
        perror("fork");
        return false;
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return false;
    }

    bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 73;
    if (!ok)
        fprintf(stderr, "app.exit child status=%d\n", status);
    return ok;
}

bool runAppSingleInstanceSmoke(int argc, char** argv)
{
    if (argc < 1 || !argv[0])
        return false;

    base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
    v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);

    std::vector<std::string> args;
    args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
    std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoStdioInitialization,
        node::ProcessInitializationFlags::kNoDefaultSignalHandling,
        node::ProcessInitializationFlags::kNoInitOpenSSL,
        node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
        node::ProcessInitializationFlags::kNoAdjustResourceLimits,
        node::ProcessInitializationFlags::kNoUseLargePages,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
    });
    if (!init || init->early_return()) {
        if (init)
            printNodeInitErrors(*init);
        return false;
    }

    std::vector<std::string> errors;
    std::vector<std::string> execArgs;
    std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(init->platform(), &errors, init->args(), execArgs);
    if (!setup) {
        for (const std::string& error : errors)
            fprintf(stderr, "node setup error: %s\n", error.c_str());
        node::TearDownOncePerProcess();
        return false;
    }

    bool ok = false;
    {
        v8::Isolate* isolate = setup->isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = setup->context();
        v8::Context::Scope contextScope(context);
        gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());

        ok = addNodeLinkedBinding(setup->env(), "electron_browser_app");
        if (ok) {
            const char scriptSource[] =
                "const { EventEmitter } = require('events');"
                "const { App } = process._linkedBinding('electron_browser_app');"
                "Object.setPrototypeOf(App.prototype, EventEmitter.prototype);"
                "globalThis.__singleInstanceEvents = [];"
                "globalThis.__singleInstanceLegacy = [];"
                "globalThis.__singleInstanceApp = new App();"
                "const app = globalThis.__singleInstanceApp;"
                "globalThis.__singleInstanceLockPath = app._getSingleInstanceLockPathForTesting();"
                "globalThis.__singleInstanceSocketPath = app._getSingleInstanceSocketPathForTesting();"
                "app.on('second-instance', function(argv, cwd) {"
                "  globalThis.__singleInstanceEvents.push({ argv, cwd });"
                "});"
                "if (!app.requestSingleInstanceLock()) throw new Error('first lock failed: ' + app._getSingleInstanceFailureReasonForTesting());"
                "if (!require('fs').existsSync(globalThis.__singleInstanceLockPath)) throw new Error('missing parent lock: ' + globalThis.__singleInstanceLockPath);"
                "if (app.makeSingleInstanceImpl(function(argString) {"
                "  const parsed = JSON.parse(argString);"
                "  globalThis.__singleInstanceLegacy.push({ argv: parsed.slice(0, -1), cwd: parsed[parsed.length - 1] });"
                "})) throw new Error('first makeSingleInstanceImpl returned second');"
                "true;";
            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
            if (result.IsEmpty()) {
                if (tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                ok = false;
            }
        }

        if (ok) {
            drainNodeLoop(setup.get(), init->platform(), isolate, 4);
            pid_t pid = fork();
            if (pid == 0) {
                execl(argv[0], argv[0], "--electron-app-single-instance-child", "--second-instance-arg", nullptr);
                _exit(127);
            }
            if (pid < 0) {
                perror("fork");
                ok = false;
            } else {
                int status = 0;
                if (waitpid(pid, &status, 0) < 0) {
                    perror("waitpid");
                    ok = false;
                } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    fprintf(stderr, "single instance child status=%d\n", status);
                    ok = false;
                }
            }
        }

        if (ok) {
            for (int i = 0; i < 200; ++i) {
                uv_run(setup->event_loop(), UV_RUN_NOWAIT);
                init->platform()->DrainTasks(isolate);

                const char pollScript[] =
                    "globalThis.__singleInstanceEvents.length === 1 &&"
                    "globalThis.__singleInstanceLegacy.length === 1;";
                if (runV8Script(context, pollScript))
                    break;
                usleep(10000);
            }

            const char verifyScript[] =
                "const event = globalThis.__singleInstanceEvents[0];"
                "const legacy = globalThis.__singleInstanceLegacy[0];"
                "if (!event || !legacy) throw new Error('missing single instance callback');"
                "if (!event.argv.includes('--second-instance-arg')) throw new Error('missing event argv ' + JSON.stringify(event.argv));"
                "if (!legacy.argv.includes('--second-instance-arg')) throw new Error('missing legacy argv ' + JSON.stringify(legacy.argv));"
                "if (typeof event.cwd !== 'string' || event.cwd.length === 0) throw new Error('bad event cwd');"
                "if (event.cwd !== legacy.cwd) throw new Error('cwd mismatch');"
                "globalThis.__singleInstanceApp.releaseSingleInstance();"
                "true;";
            ok = runV8Script(context, verifyScript);
        }

        if (ok) {
            pid_t stalePid = fork();
            if (stalePid == 0) {
                execl(argv[0], argv[0], "--electron-app-single-instance-stale-lock-child", nullptr);
                _exit(127);
            }
            if (stalePid < 0) {
                perror("fork");
                ok = false;
            } else {
                int status = 0;
                if (waitpid(stalePid, &status, 0) < 0) {
                    perror("waitpid");
                    ok = false;
                } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    fprintf(stderr, "single instance stale child status=%d\n", status);
                    ok = false;
                }
            }
            if (ok) {
                v8::Local<v8::Value> lockValue;
                if (!getObjectProperty(context, context->Global(), "__singleInstanceLockPath", &lockValue)) {
                    ok = false;
                } else {
                    v8::String::Utf8Value lockPath(isolate, lockValue);
                    if (!*lockPath || access(*lockPath, F_OK) != 0) {
                        fprintf(stderr, "missing stale single instance lock before recovery: %s\n", *lockPath ? *lockPath : "<missing>");
                        ok = false;
                    }
                }
            }
        }

        if (ok) {
            const char staleRecoveryScript[] =
                "const { App } = process._linkedBinding('electron_browser_app');"
                "const staleApp = new App();"
                "const lockPath = staleApp._getSingleInstanceLockPathForTesting();"
                "const socketPath = staleApp._getSingleInstanceSocketPathForTesting();"
                "globalThis.__staleRecoveryLockPath = lockPath;"
                "globalThis.__staleRecoverySocketPath = socketPath;"
                "if (!staleApp.requestSingleInstanceLock())"
                "  throw new Error('stale lock recovery failed: ' + staleApp._getSingleInstanceFailureReasonForTesting());"
                "staleApp.releaseSingleInstance();"
                "true;";
            ok = runV8Script(context, staleRecoveryScript);
            if (ok) {
                v8::Local<v8::Value> lockValue;
                v8::Local<v8::Value> socketValue;
                ok = getObjectProperty(context, context->Global(), "__staleRecoveryLockPath", &lockValue)
                    && getObjectProperty(context, context->Global(), "__staleRecoverySocketPath", &socketValue);
                if (ok) {
                    v8::String::Utf8Value lockPath(isolate, lockValue);
                    v8::String::Utf8Value socketPath(isolate, socketValue);
                    if (!*lockPath || access(*lockPath, F_OK) == 0) {
                        fprintf(stderr, "single instance stale recovery did not release lock: %s\n", *lockPath ? *lockPath : "<missing>");
                        ok = false;
                    }
                    if (*socketPath && access(*socketPath, F_OK) == 0) {
                        fprintf(stderr, "single instance stale recovery did not release socket: %s\n", *socketPath);
                        ok = false;
                    }
                }
            }
        }

        runV8Script(context, "if (globalThis.__singleInstanceApp) globalThis.__singleInstanceApp.releaseSingleInstance(); true;");
        drainNodeLoop(setup.get(), init->platform(), isolate, 8);
    }

    setup.reset();
    node::TearDownOncePerProcess();

    if (!ok)
        return false;

    printf("PASS electron-app-single-instance-smoke\n");
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    g_isElectronMode = true;
    if (!base::CommandLine::InitializedForCurrentProcess())
    base::CommandLine::Init(argc, argv);
    atom::AtomCommandLine::init(argc, const_cast<const char* const*>(argv));
    nodeModuleInitRegister();
    _register_electron_common_content_tracing();

    if (hasArg(argc, argv, "--electron-app-single-instance-child")) {
        base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
        v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);
        std::vector<std::string> args;
        args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
        std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
            node::ProcessInitializationFlags::kNoStdioInitialization,
            node::ProcessInitializationFlags::kNoDefaultSignalHandling,
            node::ProcessInitializationFlags::kNoInitOpenSSL,
            node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
            node::ProcessInitializationFlags::kNoAdjustResourceLimits,
            node::ProcessInitializationFlags::kNoUseLargePages,
            node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
        });
        if (!init || init->early_return())
            return 91;
        std::vector<std::string> errors;
        std::vector<std::string> execArgs;
        std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(init->platform(), &errors, init->args(), execArgs);
        if (!setup) {
            node::TearDownOncePerProcess();
            return 92;
        }
        bool ok = false;
        {
            v8::Isolate* isolate = setup->isolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = setup->context();
            v8::Context::Scope contextScope(context);
            gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());
            ok = addNodeLinkedBinding(setup->env(), "electron_browser_app");
            if (ok) {
                const char scriptSource[] =
                    "const { EventEmitter } = require('events');"
                    "const { App } = process._linkedBinding('electron_browser_app');"
                    "Object.setPrototypeOf(App.prototype, EventEmitter.prototype);"
                    "globalThis.__singleInstanceChildApp = new App();"
                    "const app = globalThis.__singleInstanceChildApp;"
                    "globalThis.__singleInstanceChildDiagnostic = 'before makeSingleInstanceImpl lock=' + app._getSingleInstanceLockPathForTesting() +"
                    "  ' socket=' + app._getSingleInstanceSocketPathForTesting();"
                    "const isSecond = app.makeSingleInstanceImpl() === true;"
                    "globalThis.__singleInstanceChildDiagnostic = 'isSecond=' + isSecond +"
                    "  ' reason=' + app._getSingleInstanceFailureReasonForTesting() +"
                    "  ' lock=' + app._getSingleInstanceLockPathForTesting() +"
                    "  ' socket=' + app._getSingleInstanceSocketPathForTesting();"
                    "if (!isSecond) {"
                    "  const reason = app._getSingleInstanceFailureReasonForTesting();"
                    "  const lockPath = app._getSingleInstanceLockPathForTesting();"
                    "  const socketPath = app._getSingleInstanceSocketPathForTesting();"
                    "  app.releaseSingleInstance();"
                    "  throw new Error('single instance child not second: ' + reason + ' lock=' + lockPath + ' socket=' + socketPath);"
                    "}"
                    "true;";
                v8::TryCatch tryCatch(isolate);
                v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
                ok = !result.IsEmpty();
                if (!ok && tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                if (!ok && !tryCatch.HasCaught())
                    runV8Script(context, "throw new Error('single instance child failed without exception: ' + (globalThis.__singleInstanceChildDiagnostic || '<none>'));");
            }
            runV8Script(context, "if (globalThis.__singleInstanceChildApp) globalThis.__singleInstanceChildApp.releaseSingleInstance(); true;");
            drainNodeLoop(setup.get(), init->platform(), isolate, 8);
        }
        setup.reset();
        node::TearDownOncePerProcess();
        return ok ? 0 : 93;
    }

    if (hasArg(argc, argv, "--electron-app-single-instance-stale-lock-child")) {
        base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
        v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);
        std::vector<std::string> args;
        args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
        std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
            node::ProcessInitializationFlags::kNoStdioInitialization,
            node::ProcessInitializationFlags::kNoDefaultSignalHandling,
            node::ProcessInitializationFlags::kNoInitOpenSSL,
            node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
            node::ProcessInitializationFlags::kNoAdjustResourceLimits,
            node::ProcessInitializationFlags::kNoUseLargePages,
            node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
        });
        if (!init || init->early_return())
            return 94;
        std::vector<std::string> errors;
        std::vector<std::string> execArgs;
        std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(init->platform(), &errors, init->args(), execArgs);
        if (!setup) {
            node::TearDownOncePerProcess();
            return 95;
        }
        bool ok = false;
        {
            v8::Isolate* isolate = setup->isolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = setup->context();
            v8::Context::Scope contextScope(context);
            gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());
            ok = addNodeLinkedBinding(setup->env(), "electron_browser_app");
            if (ok) {
                const char scriptSource[] =
                    "const { EventEmitter } = require('events');"
                    "const { App } = process._linkedBinding('electron_browser_app');"
                    "Object.setPrototypeOf(App.prototype, EventEmitter.prototype);"
                    "globalThis.__staleSingleInstanceApp = new App();"
                    "if (!globalThis.__staleSingleInstanceApp.requestSingleInstanceLock())"
                    "  throw new Error('stale child lock failed: ' + globalThis.__staleSingleInstanceApp._getSingleInstanceFailureReasonForTesting());"
                    "true;";
                v8::TryCatch tryCatch(isolate);
                v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(setup->env(), scriptSource);
                ok = !result.IsEmpty();
                if (!ok && tryCatch.HasCaught())
                    printV8Exception(isolate, tryCatch);
                if (ok)
                    drainNodeLoop(setup.get(), init->platform(), isolate, 4);
            }
        }
        _exit(ok ? 0 : 96);
    }

    if (hasArg(argc, argv, "--electron-app-exit-child")) {
        base::SingleThreadTaskExecutor taskExecutor(base::MessagePumpType::NS_RUNLOOP);
        v8::V8::InitializeExternalStartupData(argc > 0 && argv[0] ? argv[0] : nullptr);
        std::vector<std::string> args;
        args.push_back(argc > 0 && argv[0] ? argv[0] : "miniblink");
        std::shared_ptr<node::InitializationResult> init = node::InitializeOncePerProcess(args, {
            node::ProcessInitializationFlags::kNoStdioInitialization,
            node::ProcessInitializationFlags::kNoDefaultSignalHandling,
            node::ProcessInitializationFlags::kNoInitOpenSSL,
            node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
            node::ProcessInitializationFlags::kNoAdjustResourceLimits,
            node::ProcessInitializationFlags::kNoUseLargePages,
            node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput,
        });
        if (!init || init->early_return())
            return 71;
        std::vector<std::string> errors;
        std::vector<std::string> execArgs;
        std::unique_ptr<node::CommonEnvironmentSetup> setup = node::CommonEnvironmentSetup::Create(init->platform(), &errors, init->args(), execArgs);
        if (!setup) {
            node::TearDownOncePerProcess();
            return 72;
        }
        {
            v8::Isolate* isolate = setup->isolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = setup->context();
            v8::Context::Scope contextScope(context);
            gin_helper::PerIsolateData perIsolateData(isolate, setup->array_buffer_allocator().get());
            if (!addNodeLinkedBinding(setup->env(), "electron_browser_app"))
                return 72;
            const char scriptSource[] =
                "const { EventEmitter } = require('events');"
                "const { App } = process._linkedBinding('electron_browser_app');"
                "Object.setPrototypeOf(App.prototype, EventEmitter.prototype);"
                "const app = new App();"
                "app.on('before-quit', function() { process.exit(80); });"
                "app.on('window-all-closed', function() { process.exit(81); });"
                "app.on('quit', function() { process.exit(82); });"
                "app.exit(73);";
            node::LoadEnvironment(setup->env(), scriptSource);
        }
        return 74;
    }

    if (hasArg(argc, argv, "--electron-native-theme-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_native_theme")) {
            fprintf(stderr, "missing electron_browser_native_theme linked binding\n");
            return 2;
        }
        printf("PASS electron-native-theme-linked-binding\n");
    }

    if (hasArg(argc, argv, "--electron-native-theme-appearance-smoke")) {
        if (!runNativeThemeAppearanceSmoke(argc, argv))
            return 21;
    }

    if (hasArg(argc, argv, "--electron-power-monitor-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_powermonitor")) {
            fprintf(stderr, "missing electron_browser_powermonitor linked binding\n");
            return 4;
        }
        printf("PASS electron-power-monitor-linked-binding\n");
    }

    if (hasArg(argc, argv, "--electron-power-monitor-event-smoke")) {
        if (!runPowerMonitorEventSmoke(argc, argv))
            return 22;
    }

    if (hasArg(argc, argv, "--electron-global-shortcut-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_global_shortcut")) {
            fprintf(stderr, "missing electron_browser_global_shortcut linked binding\n");
            return 6;
        }
        printf("PASS electron-global-shortcut-linked-binding\n");
    }

    if (hasArg(argc, argv, "--electron-global-shortcut-dispatch-smoke")) {
        if (!runGlobalShortcutDispatchSmoke(argc, argv))
            return 24;
    }

    if (hasArg(argc, argv, "--electron-menu-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_menu")) {
            fprintf(stderr, "missing electron_browser_menu linked binding\n");
            return 25;
        }
        if (!runMenuApiSmoke(argc, argv))
            return 26;
    }

    if (hasArg(argc, argv, "--electron-native-image-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_common_nativeImage")) {
            fprintf(stderr, "missing electron_common_nativeImage linked binding\n");
            return 27;
        }
        if (!runNativeImageApiSmoke(argc, argv))
            return 28;
    }

    if (hasArg(argc, argv, "--electron-clipboard-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_common_clipboard")) {
            fprintf(stderr, "missing electron_common_clipboard linked binding\n");
            return 29;
        }
        if (!runClipboardApiSmoke(argc, argv))
            return 30;
    }

    if (hasArg(argc, argv, "--electron-shell-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_common_shell")) {
            fprintf(stderr, "missing electron_common_shell linked binding\n");
            return 31;
        }
        if (!runShellApiSmoke(argc, argv))
            return 32;
    }

    if (hasArg(argc, argv, "--electron-dialog-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_dialog")) {
            fprintf(stderr, "missing electron_browser_dialog linked binding\n");
            return 33;
        }
        if (!runDialogApiSmoke(argc, argv))
            return 34;
    }

    if (hasArg(argc, argv, "--electron-tray-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_tray")) {
            fprintf(stderr, "missing electron_browser_tray linked binding\n");
            return 35;
        }
        if (!runTrayApiSmoke(argc, argv))
            return 36;
    }

    if (hasArg(argc, argv, "--electron-protocol-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_protocol")) {
            fprintf(stderr, "missing electron_browser_protocol linked binding\n");
            return 37;
        }
        if (!runProtocolApiSmoke(argc, argv))
            return 38;
    }

    if (hasArg(argc, argv, "--electron-power-save-blocker-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_power_save_blocker")) {
            fprintf(stderr, "missing electron_browser_power_save_blocker linked binding\n");
            return 5;
        }
        printf("PASS electron-power-save-blocker-linked-binding\n");
    }

    if (hasArg(argc, argv, "--electron-power-save-blocker-lifecycle-smoke")) {
        if (!runPowerSaveBlockerLifecycleSmoke(argc, argv))
            return 23;
    }

    if (hasArg(argc, argv, "--electron-screen-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_common_screen")) {
            fprintf(stderr, "missing electron_common_screen linked binding\n");
            return 3;
        }
        printf("PASS electron-screen-linked-binding\n");
    }

    if (hasArg(argc, argv, "--electron-app-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_app")) {
            fprintf(stderr, "missing electron_browser_app linked binding\n");
            return 11;
        }
        printf("PASS electron-app-linked-binding\n");
        if (!runAppLifecycleSmoke(argc, argv))
            return 12;
        if (!runAppExitChildSmoke(argc, argv))
            return 13;
        printf("PASS electron-app-exit-smoke\n");
    }

    if (hasArg(argc, argv, "--electron-app-single-instance-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_app")) {
            fprintf(stderr, "missing electron_browser_app linked binding\n");
            return 11;
        }
        if (!runAppSingleInstanceSmoke(argc, argv))
            return 14;
    }

    if (hasArg(argc, argv, "--electron-linked-binding-runtime-smoke")) {
        if (!runLinkedBindingRuntimeSmoke(argc, argv))
            return 7;
    }

    if (hasArg(argc, argv, "--electron-base-api-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_commandline")) {
            fprintf(stderr, "missing electron_browser_commandline linked binding\n");
            return 39;
        }
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_safe_storage")) {
            fprintf(stderr, "missing electron_browser_safe_storage linked binding\n");
            return 40;
        }
        if (!electronMacNodeBridgeHasLinkedModule("electron_common_features")) {
            fprintf(stderr, "missing electron_common_features linked binding\n");
            return 41;
        }
        if (!electronMacNodeBridgeHasLinkedModule("electron_common_v8_util")) {
            fprintf(stderr, "missing electron_common_v8_util linked binding\n");
            return 42;
        }
        if (!electronMacNodeBridgeHasLinkedModule("electron_common_original_fs")) {
            fprintf(stderr, "missing electron_common_original_fs linked binding\n");
            return 43;
        }
        if (!electronMacNodeBridgeHasLinkedModule("electron_common_intl_collator")) {
            fprintf(stderr, "missing electron_common_intl_collator linked binding\n");
            return 44;
        }
        if (!runElectronBaseApiSmoke(argc, argv))
            return 45;
    }

    if (hasArg(argc, argv, "--electron-asar-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_common_asar")) {
            fprintf(stderr, "missing electron_common_asar linked binding\n");
            return 46;
        }
        if (!runElectronAsarSmoke(argc, argv))
            return 47;
    }

    if (hasArg(argc, argv, "--electron-v8-typed-array-smoke")) {
        if (!runV8TypedArraySmoke(argc, argv))
            return 9;
    }

    if (hasArg(argc, argv, "--electron-v8-shared-array-buffer-smoke")) {
        if (!runV8SharedArrayBufferSmoke(argc, argv))
            return 10;
    }

    if (hasArg(argc, argv, "--electron-node-bootstrap-smoke")) {
        if (!runNodeBootstrapSmoke(argc, argv))
            return 8;
    }

    return 0;
}
