#include "base/message_loop/message_pump_type.h"
#include "base/task/single_thread_task_executor.h"
#include "electron/common/gin_helper/per_isolate_data.h"
#include "v8/include/libplatform/libplatform.h"
#include "v8/include/v8.h"

#include <stdio.h>
#include <string.h>

#include <memory>

extern bool g_isElectronMode;

extern "C" void nodeModuleInitRegister(void);
extern "C" bool electronMacNodeBridgeHasLinkedModule(const char* name);
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

        v8::TryCatch tryCatch(isolate);
        v8::Local<v8::Object> process = v8::Object::New(isolate);
        process->Set(context,
            v8::String::NewFromUtf8(isolate, "_linkedBinding").ToLocalChecked(),
            v8::FunctionTemplate::New(isolate, linkedBindingApi)->GetFunction(context).ToLocalChecked()).ToChecked();
        context->Global()->Set(context, v8::String::NewFromUtf8(isolate, "process").ToLocalChecked(), process).ToChecked();

        v8::Local<v8::Object> nativeTheme;
        v8::Local<v8::Object> powerMonitor;
        v8::Local<v8::Object> globalShortcut;
        v8::Local<v8::Object> powerSaveBlocker;
        v8::Local<v8::Object> screen;

        ok = requireBinding(context, "electron_browser_native_theme", &nativeTheme)
            && requireFunctionProperty(context, nativeTheme, "electron_browser_native_theme", "shouldUseDarkColors")
            && requireFunctionProperty(context, nativeTheme, "electron_browser_native_theme", "startWatching")
            && requireFunctionProperty(context, nativeTheme, "electron_browser_native_theme", "stopWatching")
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
                "const psb = process._linkedBinding('electron_browser_power_save_blocker');"
                "const gs = process._linkedBinding('electron_browser_global_shortcut');"
                "const screen = process._linkedBinding('electron_common_screen');"
                "if (typeof theme.shouldUseDarkColors !== 'function') throw new Error('nativeTheme');"
                "if (typeof psb.setExecutionState !== 'function') throw new Error('powerSaveBlocker');"
                "if (typeof gs.register !== 'function') throw new Error('globalShortcut');"
                "if (typeof screen.Screen !== 'function') throw new Error('screen');"
                "true;";
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, scriptSource).ToLocalChecked();
            v8::Local<v8::Script> script;
            v8::Local<v8::Value> result;
            ok = v8::Script::Compile(context, source).ToLocal(&script)
                && script->Run(context).ToLocal(&result)
                && result->BooleanValue(isolate);
        }

        if (!ok && tryCatch.HasCaught())
            printV8Exception(isolate, tryCatch);
    }

    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();

    if (!ok)
        return false;

    printf("PASS electron-linked-binding-runtime-smoke\n");
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    g_isElectronMode = true;
    nodeModuleInitRegister();

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

    if (hasArg(argc, argv, "--electron-linked-binding-runtime-smoke")) {
        if (!runLinkedBindingRuntimeSmoke(argc, argv))
            return 7;
    }

    return 0;
}
