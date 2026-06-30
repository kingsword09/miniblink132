#include "electron/nodeblink.h"

#include "electron/common/TracingControllerImpl.h"
#include "third_party/libnode/src/env.h"
#include "third_party/libnode/src/env-inl.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libnode/src/node_buffer.h"
#include "third_party/libnode/src/node_platform.h"
#include "third_party/libuv/include/uv.h"
#include "gin/public/isolate_holder.h"
#include "v8/include/libplatform/libplatform.h"
#include "v8/src/libplatform/default-platform-wrap.h"

#include <memory>
#include <string>
#include <vector>

extern "C" NODE_EXTERN void* nodeCreateDefaultPlatform()
{
    gin::DefaultPlatformWrap* defaultPlatform = new gin::DefaultPlatformWrap();
    return defaultPlatform->GetPlatform();
}

extern "C" NODE_EXTERN void nodeDeleteNodeEnvironment(node::Environment* env)
{
    if (env)
        node::FreeEnvironment(env);
}

extern "C" NODE_EXTERN node::Environment* nodeCreateEnvironment(
    void* isolateData, void* context, const void* args, const void* execArgs, int flags)
{
    auto* contextPtr = static_cast<v8::Local<v8::Context>*>(context);
    auto* argsPtr = static_cast<const std::vector<std::string>*>(args);
    auto* execArgsPtr = static_cast<const std::vector<std::string>*>(execArgs);
    return node::CreateEnvironment(static_cast<node::IsolateData*>(isolateData),
        *contextPtr, *argsPtr, *execArgsPtr, static_cast<node::EnvironmentFlags::Flags>(flags));
}

v8::Local<v8::Object> nodeGetEnvironmentProcessObject(node::Environment* env)
{
    return env ? env->process_object() : v8::Local<v8::Object>();
}

node::Environment* nodeEnvironmentGetCurrent(v8::Isolate* isolate)
{
    return node::Environment::GetCurrent(isolate);
}

v8::Isolate* nodeEnvironmentGetV8Isolate(node::Environment* env)
{
    return env ? env->isolate() : nullptr;
}

v8::Local<v8::Context> nodeEnvironmentGetV8Context(node::Environment* env)
{
    return env ? env->context() : v8::Local<v8::Context>();
}

node::Environment* nodeEnvironmentGetByV8Context(v8::Local<v8::Context> context)
{
    return node::Environment::GetCurrent(context);
}

void nodeEnvironmentSetIsblinkCore(node::Environment* env)
{
    if (env)
        env->set_is_blink_core();
}

void nodeEnvironmentAddCustomArgs(node::Environment* env, const std::vector<std::string>& argv)
{
    if (env && !argv.empty())
        env->AddCustomArgs(argv);
}

void nodeAddElectronRequire(node::Environment*)
{
}

void nodeEnvironmentElectronPostEarlyInitialization(node::Environment* env)
{
    if (env)
        env->options()->unhandled_rejections = "warn";
}

extern "C" NODE_EXTERN BlinkMicrotaskSuppressionHandle nodeBlinkMicrotaskSuppressionEnter(v8::Isolate*)
{
    return nullptr;
}

extern "C" NODE_EXTERN void nodeBlinkMicrotaskSuppressionLeave(BlinkMicrotaskSuppressionHandle)
{
}

extern "C" NODE_EXTERN void* nodeBlinkAllocateUninitialized(size_t length)
{
    return node::Malloc<char>(length);
}

extern "C" NODE_EXTERN void nodeBlinkFree(void* data, size_t)
{
    node::Realloc<char>(static_cast<char*>(data), 0);
}

extern "C" NODE_EXTERN char* nodeBufferGetData(void* buf, size_t* len)
{
    if (!buf || !len)
        return nullptr;
    auto* value = static_cast<v8::Local<v8::Value>*>(buf);
    *len = node::Buffer::Length(*value);
    return node::Buffer::Data(*value);
}
