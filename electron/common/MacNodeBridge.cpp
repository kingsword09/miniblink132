#include "third_party/libnode/src/node.h"

#include "v8/include/v8.h"

#include <stddef.h>
#include <string.h>

namespace {

node::node_module* g_linkedModules = nullptr;

} // namespace

extern "C" void _register_electron_browser_native_theme(void);
extern "C" void _register_electron_common_screen(void);

extern "C" void node_module_register(void* module)
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
}

extern "C" bool electronMacNodeBridgeHasLinkedModule(const char* name)
{
    if (!name)
        return false;

    for (node::node_module* it = g_linkedModules; it; it = it->nm_link) {
        if (it->nm_modname && strcmp(it->nm_modname, name) == 0)
            return true;
    }

    return false;
}

extern "C" void nodeModuleInitRegister(void)
{
    _register_electron_browser_native_theme();
    _register_electron_common_screen();
}

namespace node {

v8::Local<v8::Value> MakeCallback(v8::Isolate* isolate, v8::Local<v8::Object> recv, const char* method, int argc, v8::Local<v8::Value>* argv)
{
    v8::EscapableHandleScope handleScope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::String> methodName = v8::String::NewFromUtf8(isolate, method, v8::NewStringType::kNormal).ToLocalChecked();
    v8::Local<v8::Value> callbackValue;
    if (!recv->Get(context, methodName).ToLocal(&callbackValue) || !callbackValue->IsFunction())
        return handleScope.Escape(v8::Undefined(isolate));

    v8::Local<v8::Value> result;
    if (!callbackValue.As<v8::Function>()->Call(context, recv, argc, argv).ToLocal(&result))
        return handleScope.Escape(v8::Undefined(isolate));

    return handleScope.Escape(result);
}

} // namespace node

namespace blink {
struct CloneableMessage;
} // namespace blink

namespace atom {

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
