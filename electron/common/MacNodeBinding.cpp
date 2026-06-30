#include "electron/common/NodeBinding.h"

#include "electron/nodeblink.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libuv/include/uv.h"
#include "electron/common/AtomCommandLine.h"
#include "electron/common/api/EventEmitterCaller.h"
#include "gin/dictionary.h"
#include "base/files/file_path.h"
#include "base/path_service.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace atom {

namespace {

void crash(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    fprintf(stderr, "electron crash called\n");
    abort();
}

void hang(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    for (;;) {
        usleep(1000000);
    }
}

void getProcessMemoryInfo(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    gin::Dictionary dict = gin::Dictionary::CreateEmpty(isolate);
    dict.Set("workingSetSize", 1000);
    dict.Set("peakWorkingSetSize", 1000);
    dict.Set("privateBytes", 1000);
    dict.Set("sharedBytes", 1000);
    info.GetReturnValue().Set(gin::Converter<gin::Dictionary>::ToV8(isolate, dict));
}

void getSystemMemoryInfo(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    gin::Dictionary dict = gin::Dictionary::CreateEmpty(isolate);
    dict.Set("total", 10);
    dict.Set("free", 10);
    info.GetReturnValue().Set(gin::Converter<gin::Dictionary>::ToV8(isolate, dict));
}

void getSystemVersion(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    std::string version = "macos";
    v8::Local<v8::String> result = v8::String::NewFromUtf8(args.GetIsolate(), version.c_str()).ToLocalChecked();
    args.GetReturnValue().Set(result);
}

static void MethodCallbackWrap(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Local<v8::External> v8Holder;
    gin::ConvertFromV8(info.GetIsolate(), info.Data(), &v8Holder);
    std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>* func =
        (std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>*)v8Holder->Value();
    (*func)(info);
}

static void bindMethod(v8::Isolate* isolate, v8::Local<v8::Object> object,
    const char* name, const std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>&& callback)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::External> wrap = v8::External::New(isolate,
        new std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>(callback));
    v8::Local<v8::Function> func =
        v8::FunctionTemplate::New(isolate, MethodCallbackWrap, wrap)->GetFunction(context).ToLocalChecked();
    const v8::NewStringType type = v8::NewStringType::kInternalized;
    v8::Local<v8::String> nameString = v8::String::NewFromUtf8(isolate, name, type).ToLocalChecked();
    object->Set(context, nameString, func);
    func->SetName(nameString);
}

} // namespace

NodeBindings::NodeBindings(bool isBrowser)
    : m_isBrowser(isBrowser)
    , m_uvLoop(nullptr)
    , m_uvEnv(nullptr)
    , m_callNextTickAsync(new uv_async_t())
{
}

NodeBindings::~NodeBindings()
{
    if (m_uvEnv)
        nodeDeleteNodeEnvironment(m_uvEnv);
    if (m_isolateData)
        node::FreeIsolateData(m_isolateData);
    delete m_callNextTickAsync;
}

void NodeBindings::initNodeEnv()
{
    std::vector<std::string> args = AtomCommandLine::argv();
    std::vector<std::string> execArgv;
    std::vector<std::string> errors;
    uint64_t processFlags = node::ProcessFlags::kNoFlags;
    processFlags |= node::ProcessFlags::kEnableStdioInheritance;
    args.push_back("--no-experimental-detect-module");
    node::InitializeNodeWithArgs(&args, &execArgv, &errors,
        static_cast<node::ProcessFlags::Flags>(processFlags));
}

void NodeBindings::bindFunction(v8::Isolate* isolate, v8::Local<v8::Object> object)
{
    NodeBindings* self = this;
    bindMethod(isolate, object, "crash", &crash);
    bindMethod(isolate, object, "hang", &hang);
    bindMethod(isolate, object, "getProcessMemoryInfo", &getProcessMemoryInfo);
    bindMethod(isolate, object, "getSystemMemoryInfo", &getSystemMemoryInfo);
    bindMethod(isolate, object, "activateUvLoop",
        [self](const v8::FunctionCallbackInfo<v8::Value>& info) {
            self->activateUVLoop(info.GetIsolate());
        });
    bindMethod(isolate, object, "getSystemVersion", getSystemVersion);

    gin::Dictionary processObject = gin::Dictionary(isolate, object);
    processObject.Set("sandboxed", false);
    if (!m_processObjInfo.isBrowserProcess)
        processObject.Set("contextIsolated", m_processObjInfo.isContextIsolated);
}

node::Environment* NodeBindings::createEnvironment(v8::Local<v8::Context> context)
{
    v8::Isolate* isolate = context->GetIsolate();
    uv_async_init(m_uvLoop, m_callNextTickAsync, onCallNextTick);
    m_callNextTickAsync->data = this;

    std::vector<std::string> args = AtomCommandLine::argv();
    std::string processType = m_isBrowser ? "browser" : "renderer";
    base::FilePath exePath;
    base::PathService::Get(base::BasePathKey::FILE_EXE, &exePath);
    std::string resourcesPath = exePath.DirName().AppendASCII("resources").AsUTF8Unsafe();

    if (args.size() > 1) {
        resourcesPath = args[1];
        size_t pos = resourcesPath.find("resources");
        if (pos != std::string::npos)
            resourcesPath = resourcesPath.substr(0, pos + 9);
    }

    std::string scriptPath = resourcesPath + "/" + processType + "/init.js";
    args.insert(args.begin() + 1, scriptPath);

    if (!m_isolateData)
        m_isolateData = node::CreateIsolateData(isolate, m_uvLoop);

    uint64_t flags = node::EnvironmentFlags::kDefaultFlags
        | node::EnvironmentFlags::kNoGlobalSearchPaths;

    std::vector<std::string> execArgs;
    node::Environment* env = node::CreateEnvironment(m_isolateData, context,
        args, execArgs, static_cast<node::EnvironmentFlags::Flags>(flags));
    nodeEnvironmentElectronPostEarlyInitialization(env);
    if (!m_isBrowser)
        nodeEnvironmentSetIsblinkCore(env);

    m_uvEnv = env;

    gin::Dictionary process(isolate, nodeGetEnvironmentProcessObject(env));
    process.Set("type", processType);
    process.Set("resourcesPath", resourcesPath);
    if (!m_isBrowser)
        process.Set("_noBrowserGlobals", resourcesPath);

    m_processObjInfo.isBrowserProcess = m_isBrowser;
    bindFunction(isolate, nodeGetEnvironmentProcessObject(env));

    return env;
}

void NodeBindings::loadEnvironment(node::Environment* evn)
{
    node::LoadEnvironment(evn, node::StartExecutionCallback{});
    mate::emitEvent(nodeEnvironmentGetV8Isolate(evn),
        nodeGetEnvironmentProcessObject(evn), "loaded");
}

void NodeBindings::activateUVLoop(v8::Isolate* isolate)
{
    node::Environment* env = nodeEnvironmentGetCurrent(isolate);
    if (!env)
        return;
    if (std::find(m_pendingNextTicks.begin(), m_pendingNextTicks.end(), env) !=
        m_pendingNextTicks.end())
        return;
    m_pendingNextTicks.push_back(env);
    uv_async_send(m_callNextTickAsync);
}

void NodeBindings::onCallNextTick(uv_async_t* handle)
{
    NodeBindings* self = static_cast<NodeBindings*>(handle->data);
    for (auto* env : self->m_pendingNextTicks) {
        v8::Local<v8::Context> context = nodeEnvironmentGetV8Context(env);
        if (!context->GetMicrotaskQueue())
            continue;
        v8::Isolate* isolate = nodeEnvironmentGetV8Isolate(env);
        v8::Context::Scope contextScope(context);
        v8::HandleScope handleScope(isolate);
        v8::MicrotasksScope microtasksScope(context, v8::MicrotasksScope::kRunMicrotasks);
        node::CallbackScope scope(isolate, v8::Object::New(isolate), {0, 0});
    }
    self->m_pendingNextTicks.clear();
}

} // namespace atom
