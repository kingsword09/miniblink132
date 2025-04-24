
#include "ipc_lite/ipc_channel_proxy.h"
#include "ipc_lite/ipc_message_macros.h"
#include "electron/browser/api/MessageFilterWrap.h"
#include "electron/browser/api/PostMessageUtil.h"
#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/IdLiveDetect.h"
#include "electron/common/NodeBinding.h"
#include "electron/common/NodeThread.h"
#include "electron/common/AtomVersion.h"
#include "electron/common/V8Util.h"
#include "electron/common/IoThread.h"
#include "electron/common/ipc/UtilityProcessMsgs.h"
#include "electron/common/api/EventEmitter.h"
#include "electron/common/api/EventEmitterCaller.h"
#include "electron/common/gin_helper/dictionary.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/public/gin_embedders.h"
#include "electron/common/gin_helper/public/wrapper_info.h"
#include "electron/common/gin_helper/data_object_builder.h"
#include "gin/handle.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/blink/public/common/messaging/cloneable_message.h"
#include "third_party/blink/public/common/messaging/message_port_descriptor.h"
#include "third_party/blink/public/common/messaging/transferable_message.h"
#include "third_party/blink/public/common/messaging/transferable_message_mojom_traits.h"
//#include "third_party/blink/public/mojom/messaging/transferable_message.mojom.h"
#include "third_party/blink/public/mojom/messaging/transferable_message.mojom-blink.h"
#include "mojo/public/cpp/bindings/connector.h"
#include "base/task/sequenced_task_runner.h"
#include "base/strings/string_split.h"
#include "base/process/process.h"
#include "base/process/launch.h"
#include "base/strings/stringprintf.h"
#include "base/command_line.h"

extern "C" MojoResult MojoBindIpcChannelProxy(int64_t pid, void* /* IPC::ChannelProxy*/ channelProxy);
extern "C" MojoResult MojoChangeToRemoteClientMode(MojoHandle handle, int64_t pid, MojoHandle* out);

namespace atom {

bool isValidWrappable(const v8::Local<v8::Value>& val);

// // Main process
// const child = utilityProcess.fork(path.join(__dirname, 'test.js'));
// child.postMessage({ message: 'hello' });
// child.on('message', (data) = > {
//     console.log(data) // hello world!
// });
// // Child process
// process.parentPort.on('message', (e) = > {
//     process.parentPort.postMessage(`${e.data} world!`)
// });

// 本类产生的对象一般都不会销毁
class ApiParentPort : public mate::EventEmitter<ApiParentPort>, public MessageFilterInterface, public mojo::MessageReceiver {
public:
    static void init(v8::Isolate* isolate, v8::Local<v8::Object> target, node::Environment* env);
    static void create(const v8::FunctionCallbackInfo<v8::Value>& info);

    explicit ApiParentPort(v8::Isolate* isolate, v8::Local<v8::Object> wrapper, const gin_helper::Dictionary& options);
    ~ApiParentPort();

    void onRecvParentMessagePort(uintptr_t pipe);
    void closePipe();

    // MessageFilterWrap
    /*virtual*/ bool onMessageReceived(const IPC::Message& message) override;
    /*virtual*/ void onChannelConnected(int32 peerPid) override
    {
        m_channel = nullptr;
    }
    /*virtual*/ void onChannelError() override
    {
        m_channel = nullptr;
        HANDLE handle = ::OpenProcess(PROCESS_ALL_ACCESS, 0, ::GetCurrentProcessId());
        ::TerminateProcess(handle, -1);
    }
    /*virtual*/ void onChannelClosing() override
    {
        m_channel = nullptr;
    }

    // mojo::MessageReceiver
    /*virtual*/ bool Accept(mojo::Message* message) override;

    static void newFunction(const v8::FunctionCallbackInfo<v8::Value>& args);

public:
    void onParentMsg(const std::vector<char>& msg);
    void _sendApi(const v8::FunctionCallbackInfo<v8::Value>& info);

    scoped_refptr<base::SequencedTaskRunner> m_mainThread;
    IPC::ChannelProxy* m_channel = nullptr;
    scoped_refptr<MessageFilterWrap> m_messageFilterWrap;
    std::unique_ptr<mojo::Connector> m_connector;

    int64_t m_parentProcessId = 0;

    static gin_helper::WrapperInfo kWrapperInfo;
};

// 只能有一个子进程
static ApiParentPort* s_childProcessParentPort = nullptr;

gin_helper::WrapperInfo ApiParentPort::kWrapperInfo = { gin_helper::GinEmbedder::kEmbedderNativeGin };
v8::Persistent<v8::Function> s_ParentPortConstructor;

static void initializeParentPortApi(v8::Local<v8::Object> target, v8::Local<v8::Value> unused, v8::Local<v8::Context> context, const NodeNative* native)
{
    ApiParentPort::init(context->GetIsolate(), target, nullptr);
}

void ApiParentPort::init(v8::Isolate* isolate, v8::Local<v8::Object> target, node::Environment* env)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::FunctionTemplate> prototype = v8::FunctionTemplate::New(isolate, ApiParentPort::newFunction);

    prototype->SetClassName(v8::String::NewFromUtf8(isolate, "ParentPort").ToLocalChecked());
    gin_helper::ObjectTemplateBuilder builder(isolate, prototype->InstanceTemplate());
    builder.SetMethod("_send", &ApiParentPort::_sendApi);

    v8::Local<v8::Function> prototypFunc = prototype->GetFunction(context).ToLocalChecked();
    s_ParentPortConstructor.Reset(isolate, prototypFunc);

    base::CommandLine* cmdLine = base::CommandLine::ForCurrentProcess();
    if (cmdLine->HasSwitch(kElectronUtilProcChannelId)) { // 本进程属于UtilityProcess才会绑定
        target->Set(context, v8::String::NewFromUtf8(isolate, "ParentPort").ToLocalChecked(), prototypFunc);
    }
}

void ApiParentPort::newFunction(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    MessageBoxA(0, "ApiParentPort::newFunction", 0, 0);

    CHECK(args.IsConstructCall());
    if (args.Length() != 1)
        return;

    gin_helper::Dictionary options(args.GetIsolate(), args[0]->ToObject(context).ToLocalChecked());
    new ApiParentPort(isolate, args.This(), options);
    args.GetReturnValue().Set(args.This());
}

ApiParentPort::ApiParentPort(v8::Isolate* isolate, v8::Local<v8::Object> wrapper, const gin_helper::Dictionary& options)
{
    gin_helper::Wrappable<ApiParentPort>::InitWith(isolate, wrapper);
    m_mainThread = base::SequencedTaskRunner::GetCurrentDefault();
    m_messageFilterWrap = new MessageFilterWrap(this);

    //bool isChildProcess = false;
    //options.GetBydefaultVal("isChildProcess", "", &isChildProcess);

    CHECK(!s_childProcessParentPort);
    s_childProcessParentPort = this;

    base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
    std::string channelId = cmd->GetSwitchValueASCII(kElectronUtilProcChannelId);
    m_channel = (new IPC::ChannelProxy(channelId, IPC::Channel::MODE_CLIENT, nullptr, IoThread::get()->taskRunner()));
    m_channel->AddFilter(m_messageFilterWrap.get());

    std::vector<std::string> tempStr = base::SplitString(channelId, "|", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    CHECK(tempStr.size() == 2);
    int64_t parentProcessId = 0;
    CHECK(base::StringToInt64(tempStr[0], &parentProcessId));
    MojoBindIpcChannelProxy(parentProcessId, m_channel);
    m_parentProcessId = parentProcessId;
}

ApiParentPort::~ApiParentPort()
{
    if (m_channel)
        m_channel->RemoveFilter(m_messageFilterWrap.get());
}

void ApiParentPort::create(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() != 1)
        return;
    if (!info[0]->IsObject())
        return;
    v8::Local<v8::Object> arg0 = info[0].As<v8::Object>();
    gin_helper::Dictionary options(isolate, arg0);
    const int argc = 1;
    v8::Local<v8::Value> argv[argc] = { arg0 }; // { gin_helper::ConvertToV8(isolate, options) };
    v8::Local<v8::Function> constructorFunction = v8::Local<v8::Function>::New(isolate, s_ParentPortConstructor);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    v8::MaybeLocal<v8::Object> obj = constructorFunction->NewInstance(context, argc, argv); // call into ApiUtilityProcess::ApiUtilityProcess
    if (obj.IsEmpty())
        return;

    v8::Local<v8::Object> objV8 = obj.ToLocalChecked();
    info.GetReturnValue().Set(objV8);
}

void ApiParentPort::_sendApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    postMessageHelper(m_connector.get(), info);
}

bool ApiParentPort::onMessageReceived(const IPC::Message& message)
{
    bool handled = true;
    bool deserializeSuccess = true;
    IPC_BEGIN_MESSAGE_MAP_EX(ApiParentPort, message, deserializeSuccess)
    //IPC_MESSAGE_HANDLER(UtilityProcessMsg_PostToChildMessage, onParentMsg)
    IPC_MESSAGE_HANDLER(UtilityProcessMsg_PostMessagePortToChildProcess, onRecvParentMessagePort)
    IPC_MESSAGE_UNHANDLED(handled = false)
    IPC_END_MESSAGE_MAP_EX()

    return handled;
}

bool ApiParentPort::Accept(mojo::Message* mojoMessage)
{
    return onAcceptHelper(getWrapper(), mojoMessage);
    //     blink::TransferableMessage message;
    //     if (!blink::mojom::blink::TransferableMessage::DeserializeFromMessage(std::move(*mojoMessage), &message))
    //         return false;
    //
    // //     v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    // //     v8::HandleScope handle_scope(isolate);
    // //     auto wrapped_ports = MessagePort::EntanglePorts(isolate, std::move(message.ports));
    // //     v8::Local<v8::Value> message_value = electron::DeserializeV8Value(isolate, message);
    // //     v8::Local<v8::Object> self;
    // //     if (!GetWrapper(isolate).ToLocal(&self))
    // //         return false;
    // //     auto event = gin::DataObjectBuilder(isolate)
    // //         .Set("data", message_value)
    // //         .Set("ports", wrapped_ports)
    // //         .Build();
    // //     gin_helper::EmitEvent(isolate, self, "message", event);
    //
    //     v8::Isolate* isolate = v8::Isolate::GetCurrent();
    //     v8::HandleScope handleScope(isolate);
    //     v8::Local<v8::Value> messageValue = deserializeV8Value(isolate, message.encoded_message);
    //
    //     v8::Local<v8::Object> evt = gin_helper::DataObjectBuilder(isolate)
    //         .Set("data", messageValue)
    //         //.Set("ports", wrapped_ports)
    //         .Build();
    //     mate::emitEvent(isolate, getWrapper(), "message", evt);
    //
    //     return true;
}

void ApiParentPort::closePipe()
{
    m_channel = nullptr;
}

// 这个是在io线程执行的
void ApiParentPort::onRecvParentMessagePort(uintptr_t pipe)
{
    m_mainThread->PostTask(FROM_HERE,
        base::BindOnce(
            [](ApiParentPort* self, uintptr_t pipe) {
                MojoHandle localHandle = 0;
                MojoChangeToRemoteClientMode(pipe, self->m_parentProcessId, &localHandle);

                mojo::MessagePipeHandle h(localHandle);
                mojo::ScopedMessagePipeHandle scopedMsgPipeHandle(std::move(h));
                self->m_connector = std::make_unique<mojo::Connector>(std::move(scopedMsgPipeHandle), mojo::Connector::SINGLE_THREADED_SEND);
                self->m_connector->PauseIncomingMethodCallProcessing();
                self->m_connector->set_incoming_receiver(self);
                self->m_connector->set_connection_error_handler(base::BindOnce(&ApiParentPort::closePipe, base::Unretained(self)));
                self->m_connector->StartReceiving(self->m_mainThread);
            },
            base::Unretained(this), pipe));
}

// 本函数被废弃了，用Accept
void ApiParentPort::onParentMsg(const std::vector<char>& msg)
{
    base::span<const uint8_t> data((const uint8_t*)msg.data(), msg.size());
    v8::Local<v8::Value> msvV8 = deserializeV8Value(v8::Isolate::GetCurrent(), data);
    mate::EventEmitter<ApiParentPort>::emit("message", msvV8);
}

static const char ParentPortSricpt[] = "exports = {};";

static NodeNative nativeParentPortNative { "ParentPort", ParentPortSricpt, sizeof(ParentPortSricpt) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(atom_browser_parent_port, initializeParentPortApi, &nativeParentPortNative)

} // atom