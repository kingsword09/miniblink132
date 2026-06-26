#include "third_party/libnode/src/node.h"

#include "v8/include/v8.h"

#include <stddef.h>
#include <string.h>

#include <memory>
#include <string>

namespace {

node::node_module* g_linkedModules = nullptr;

node::node_module* findLinkedModule(const char* name)
{
    if (!name)
        return nullptr;

    for (node::node_module* it = g_linkedModules; it; it = it->nm_link) {
        if (it->nm_modname && strcmp(it->nm_modname, name) == 0)
            return it;
    }

    return nullptr;
}

} // namespace

extern "C" void _register_electron_browser_native_theme(void);
extern "C" void _register_electron_browser_app(void);
extern "C" void _register_electron_browser_global_shortcut(void);
extern "C" void _register_electron_browser_powermonitor(void);
extern "C" void _register_electron_browser_power_save_blocker(void);
extern "C" void _register_electron_common_screen(void);

extern "C" void electronMacNodeBridgeRegisterModule(void* module)
{
    node::node_module* nodeModule = static_cast<node::node_module*>(module);
    if (!nodeModule)
        return;

    for (node::node_module* it = g_linkedModules; it; it = it->nm_link) {
        if (it == nodeModule)
            return;
        if (it->nm_modname && nodeModule->nm_modname && strcmp(it->nm_modname, nodeModule->nm_modname) == 0)
            return;
    }

    nodeModule->nm_flags = node::ModuleFlags::kLinked;
    nodeModule->nm_link = g_linkedModules;
    g_linkedModules = nodeModule;
    node::node_module_register(module);
}

extern "C" bool electronMacNodeBridgeHasLinkedModule(const char* name)
{
    return findLinkedModule(name) != nullptr;
}

extern "C" bool electronMacNodeBridgeGetLinkedModuleRegistration(const char* name, node::addon_context_register_func* registerFunc, void** priv)
{
    node::node_module* module = findLinkedModule(name);
    if (!module || !module->nm_context_register_func || !registerFunc)
        return false;

    *registerFunc = module->nm_context_register_func;
    if (priv)
        *priv = module->nm_priv;
    return true;
}

extern "C" bool electronMacNodeBridgeGetLinkedBinding(const char* name, v8::Local<v8::Context> context, v8::Local<v8::Value>* out)
{
    node::node_module* module = findLinkedModule(name);
    if (!module || !out)
        return false;

    v8::Isolate* isolate = context->GetIsolate();
    v8::Local<v8::Object> moduleObject = v8::Object::New(isolate);
    v8::Local<v8::Object> exports = v8::Object::New(isolate);
    v8::Local<v8::String> exportsKey = v8::String::NewFromUtf8(isolate, "exports").ToLocalChecked();
    if (moduleObject->Set(context, exportsKey, exports).IsNothing())
        return false;

    if (module->nm_context_register_func)
        module->nm_context_register_func(exports, moduleObject, context, module->nm_priv);
    else if (module->nm_register_func)
        module->nm_register_func(exports, moduleObject, module->nm_priv);
    else
        return false;

    v8::Local<v8::Value> effectiveExports;
    if (!moduleObject->Get(context, exportsKey).ToLocal(&effectiveExports))
        return false;

    *out = effectiveExports;
    return true;
}

extern "C" void nodeModuleInitRegister(void)
{
    _register_electron_browser_native_theme();
    _register_electron_browser_app();
    _register_electron_browser_global_shortcut();
    _register_electron_browser_powermonitor();
    _register_electron_browser_power_save_blocker();
    _register_electron_common_screen();
}

bool g_isElectronMode = false;

std::shared_ptr<v8::TaskRunner> nodePlatformGetForegroundTaskRunner(v8::Isolate*)
{
    return nullptr;
}

bool nodePlatformIdleTasksEnabled(v8::Isolate*)
{
    return true;
}

namespace node {

bool g_disable_has_run_bootstrapping_code_error = false;

} // namespace node

namespace blink {
struct CloneableMessage;
} // namespace blink

namespace atom {

unsigned char AsarJs[32262 + 1] = "exports = {};";

void bindMbConsoleLog(v8::Local<v8::Context>)
{
}

void patchProcessObject(v8::Local<v8::Object>)
{
}

void PreEvaluateModule()
{
}

void PostEvaluateModule()
{
}

bool checkMiniElectronAsarResStat(const std::string&, int* rc, std::string* result)
{
    if (rc)
        *rc = -1;
    if (result)
        result->clear();
    return false;
}

bool serializeV8Value(v8::Isolate*, v8::Local<v8::Value>, blink::CloneableMessage*)
{
    return false;
}

v8::Local<v8::Value> deserializeV8Value(v8::Isolate* isolate, const blink::CloneableMessage&)
{
    return v8::Null(isolate);
}

} // namespace atom

extern "C" char* nodeBufferGetData(void* buf, size_t* len)
{
    if (len)
        *len = 0;
    if (!buf)
        return nullptr;

    v8::Local<v8::Value>* value = static_cast<v8::Local<v8::Value>*>(buf);
    if (value->IsEmpty())
        return nullptr;

    if ((*value)->IsArrayBufferView()) {
        v8::Local<v8::ArrayBufferView> view = value->As<v8::ArrayBufferView>();
        if (len)
            *len = view->ByteLength();
        return static_cast<char*>(view->Buffer()->Data()) + view->ByteOffset();
    }

    if ((*value)->IsArrayBuffer()) {
        v8::Local<v8::ArrayBuffer> buffer = value->As<v8::ArrayBuffer>();
        if (len)
            *len = buffer->ByteLength();
        return static_cast<char*>(buffer->Data());
    }

    return nullptr;
}
