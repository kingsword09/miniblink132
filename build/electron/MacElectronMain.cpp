#include "base/message_loop/message_pump_type.h"
#include "base/task/single_thread_task_executor.h"
#include "electron/common/gin_helper/per_isolate_data.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libuv/include/uv.h"
#include "v8/include/libplatform/libplatform.h"
#include "v8/include/v8.h"

#include <stdio.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

extern bool g_isElectronMode;

extern "C" void nodeModuleInitRegister(void);
extern "C" bool electronMacNodeBridgeHasLinkedModule(const char* name);
extern "C" bool electronMacNodeBridgeGetLinkedModuleRegistration(const char* name, node::addon_context_register_func* registerFunc, void** priv);
extern "C" bool electronMacNodeBridgeGetLinkedBinding(const char* name, v8::Local<v8::Context> context, v8::Local<v8::Value>* out);

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
            && requireFunctionProperty(context, screen, "electron_common_screen", "Screen");

        if (ok) {
            const char scriptSource[] =
                "const theme = process._linkedBinding('electron_browser_native_theme');"
                "const app = process._linkedBinding('electron_browser_app');"
                "const psb = process._linkedBinding('electron_browser_power_save_blocker');"
                "const gs = process._linkedBinding('electron_browser_global_shortcut');"
                "const screen = process._linkedBinding('electron_common_screen');"
                "if (typeof theme.shouldUseDarkColors !== 'function') throw new Error('nativeTheme');"
                "if (typeof app.App !== 'function') throw new Error('app');"
                "if (typeof psb.setExecutionState !== 'function') throw new Error('powerSaveBlocker');"
                "if (typeof gs.register !== 'function') throw new Error('globalShortcut');"
                "if (typeof screen.Screen !== 'function') throw new Error('screen');"
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
            && addNodeLinkedBinding(setup->env(), "electron_common_screen");

        if (ok) {
            const char scriptSource[] =
                "const appBinding = process._linkedBinding('electron_browser_app');"
                "const theme = process._linkedBinding('electron_browser_native_theme');"
                "const powerMonitor = process._linkedBinding('electron_browser_powermonitor');"
                "const globalShortcut = process._linkedBinding('electron_browser_global_shortcut');"
                "const powerSaveBlocker = process._linkedBinding('electron_browser_power_save_blocker');"
                "const screen = process._linkedBinding('electron_common_screen');"
                "if (typeof appBinding.App !== 'function') throw new Error('app');"
                "if (typeof theme.shouldUseDarkColors !== 'function') throw new Error('nativeTheme');"
                "if (typeof powerMonitor.ApiPowerMonitor !== 'function') throw new Error('powerMonitor');"
                "if (typeof globalShortcut.register !== 'function') throw new Error('globalShortcut');"
                "if (typeof powerSaveBlocker.setExecutionState !== 'function') throw new Error('powerSaveBlocker');"
                "if (typeof screen.Screen !== 'function') throw new Error('screen');"
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

bool runAppLifecycleSmoke(int argc, char** argv)
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

} // namespace

int main(int argc, char** argv)
{
    g_isElectronMode = true;
    nodeModuleInitRegister();

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

    if (hasArg(argc, argv, "--electron-power-monitor-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_powermonitor")) {
            fprintf(stderr, "missing electron_browser_powermonitor linked binding\n");
            return 4;
        }
        printf("PASS electron-power-monitor-linked-binding\n");
    }

    if (hasArg(argc, argv, "--electron-global-shortcut-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_global_shortcut")) {
            fprintf(stderr, "missing electron_browser_global_shortcut linked binding\n");
            return 6;
        }
        printf("PASS electron-global-shortcut-linked-binding\n");
    }

    if (hasArg(argc, argv, "--electron-power-save-blocker-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_power_save_blocker")) {
            fprintf(stderr, "missing electron_browser_power_save_blocker linked binding\n");
            return 5;
        }
        printf("PASS electron-power-save-blocker-linked-binding\n");
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

    if (hasArg(argc, argv, "--electron-linked-binding-runtime-smoke")) {
        if (!runLinkedBindingRuntimeSmoke(argc, argv))
            return 7;
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
