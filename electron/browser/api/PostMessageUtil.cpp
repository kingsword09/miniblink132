
#include "electron/browser/api/PostMessageUtil.h"

#include "electron/browser/api/ApiMessagePortMain.h"
#include "electron/common/V8Util.h"
#include "third_party/blink/public/common/messaging/cloneable_message.h"
#include "third_party/blink/public/common/messaging/message_port_descriptor.h"
#include "third_party/blink/public/common/messaging/transferable_message.h"
#include "third_party/blink/public/common/messaging/transferable_message_mojom_traits.h"
#include "third_party/blink/public/mojom/messaging/transferable_message.mojom-blink.h"
#include "electron/common/gin_helper/data_object_builder.h"
#include "electron/common/gin_helper/public/wrapper_info.h"
#include "electron/common/gin_helper/public/gin_embedders.h"

namespace content {
void printCallstack();
}

namespace atom {

bool isValidWrappable(const v8::Local<v8::Value>& val)
{
    if (!val->IsObject())
        return false;

    v8::Local<v8::Object> port = val.As<v8::Object>();
    int count = port->InternalFieldCount();
    if (count != gin_helper::InternalFields::kNumberOfInternalFields)
        return false;

    const auto* info = static_cast<gin_helper::WrapperInfo*>(port->GetAlignedPointerFromInternalField(gin_helper::InternalFields::kWrapperInfoIndex));

    return info && info->embedder == gin_helper::GinEmbedder::kEmbedderNativeGin;
}

bool postMessageHelper(mojo::Connector* connector, const v8::FunctionCallbackInfo<v8::Value>& info)
{
    if (info.Length() <= 0)
        return false;

    v8::Isolate* isolate = info.GetIsolate();

    blink::TransferableMessage transferableMsg;
    if (!serializeV8Value(info.GetIsolate(), info[0], &transferableMsg)) // SerializeV8Value sets an exception.
        return false;

    content::printCallstack();

    std::vector<ApiMessagePortMain*> wrappedPorts;
    if (info.Length() == 2) {
        v8::Local<v8::Value> transferables = info[1];
        std::vector<v8::Local<v8::Value>> wrappedPortValues;
        if (!gin_helper::ConvertFromV8(isolate, transferables, &wrappedPortValues)) {
            isolate->ThrowException(gin_helper::StringToV8(isolate, "transferables must be an array of MessagePorts"));
            return false;
        }

        for (size_t i = 0; i < wrappedPortValues.size(); ++i) {
            if (!isValidWrappable(wrappedPortValues[i])) {
                std::string temp = "Port at index " + base::NumberToString(i) + " is not a valid port";
                isolate->ThrowException(gin_helper::StringToV8(isolate, temp));
                return false;
            }
        }

        if (!gin_helper::ConvertFromV8(isolate, transferables, &wrappedPorts)) {
            isolate->ThrowException(gin_helper::StringToV8(isolate, "Passed an invalid MessagePort"));
            return false;
        }

        bool threwException = false;
        transferableMsg.ports = ApiMessagePortMain::disentanglePorts(isolate, wrappedPorts, &threwException);
        if (threwException)
            return false;
    }

    mojo::Message mojoMessage = blink::mojom::blink::TransferableMessage::WrapAsMessage(std::move(transferableMsg));
    connector->Accept(&mojoMessage);
    return true;
}

bool onAcceptHelper(v8::Local<v8::Object> wrap, mojo::Message* mojoMessage)
{
    blink::TransferableMessage message;
    if (!blink::mojom::blink::TransferableMessage::DeserializeFromMessage(std::move(*mojoMessage), &message))
        return false;

    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Value> messageValue = deserializeV8Value(isolate, message.encoded_message);

    //     v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    //     v8::HandleScope handle_scope(isolate);
    std::vector<ApiMessagePortMain*> wrappedPorts = ApiMessagePortMain::entanglePorts(isolate, std::move(message.ports));
    //     v8::Local<v8::Value> message_value = electron::DeserializeV8Value(isolate, message);
    //     v8::Local<v8::Object> self;
    //     if (!GetWrapper(isolate).ToLocal(&self))
    //         return false;
    //     auto event = gin::DataObjectBuilder(isolate)
    //         .Set("data", message_value)
    //         .Set("ports", wrapped_ports)
    //         .Build();
    //     gin_helper::EmitEvent(isolate, self, "message", event);

    v8::Local<v8::Object> evt = gin_helper::DataObjectBuilder(isolate).Set("data", messageValue).Set("ports", wrappedPorts).Build();
    mate::emitEvent(isolate, wrap, "message", evt);

    return true;
}

}