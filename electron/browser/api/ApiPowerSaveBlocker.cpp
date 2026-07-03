#include "electron/common/NodeRegisterHelp.h"
#include "third_party/libnode/src/node_binding.h"
#include "v8/include/v8.h"

#include <windows.h>

#include <stddef.h>
#include <stdint.h>

namespace {

struct NodeNative {
    const char* name;
    const char* source;
    size_t sourceLen;
};

void setExecutionStateApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (info.Length() < 1 || !info[0]->IsNumber()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8(isolate, "execution state is required").ToLocalChecked()));
        return;
    }

    v8::Maybe<int64_t> maybeState = info[0]->IntegerValue(context);
    if (maybeState.IsNothing())
        return;

    int64_t state = maybeState.FromJust();
    if (state < 0 || state > UINT32_MAX) {
        isolate->ThrowException(v8::Exception::RangeError(
            v8::String::NewFromUtf8(isolate, "execution state is out of range").ToLocalChecked()));
        return;
    }

    EXECUTION_STATE previous = SetThreadExecutionState(static_cast<EXECUTION_STATE>(state));
    info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(isolate, previous));
}

void initializePowerSaveBlockerApi(v8::Local<v8::Object> exports, v8::Local<v8::Value>, v8::Local<v8::Context> context, const NodeNative*)
{
    v8::Isolate* isolate = context->GetIsolate();

    exports->Set(context,
        v8::String::NewFromUtf8(isolate, "setExecutionState").ToLocalChecked(),
        v8::FunctionTemplate::New(isolate, setExecutionStateApi)->GetFunction(context).ToLocalChecked()).ToChecked();
}

const char PowerSaveBlockerScript[] = "exports = {};";
NodeNative nativePowerSaveBlockerNative { "ApiPowerSaveBlocker", PowerSaveBlockerScript, sizeof(PowerSaveBlockerScript) - 1 };

} // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_browser_power_save_blocker, initializePowerSaveBlockerApi, &nativePowerSaveBlockerNative)
