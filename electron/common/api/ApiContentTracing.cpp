#include "electron/nodeblink.h"
#include "electron/common/NodeRegisterHelp.h"
#include "third_party/libnode/src/node_binding.h"
#include "v8/include/v8.h"

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/trace_event/trace_config.h"
#include "base/trace_event/trace_event_lightweight.h"
#include "base/trace_event/trace_log.h"

#include <string>
#include <string_view>

namespace {

std::string g_recordingCategory = "electron,miniblink";

std::string firstEnabledCategoryFromFilter(const std::string& categoryFilter)
{
    size_t tokenStart = 0;
    while (tokenStart <= categoryFilter.size()) {
        size_t tokenEnd = categoryFilter.find(',', tokenStart);
        if (tokenEnd == std::string::npos)
            tokenEnd = categoryFilter.size();

        std::string_view token(categoryFilter.data() + tokenStart, tokenEnd - tokenStart);
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
            token.remove_prefix(1);
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
            token.remove_suffix(1);

        if (!token.empty() && token.front() != '-') {
            if (token.front() == '+')
                token.remove_prefix(1);
            if (!token.empty() && token != "*")
                return std::string(token);
        }

        if (tokenEnd == categoryFilter.size())
            break;
        tokenStart = tokenEnd + 1;
    }

    return "electron,miniblink";
}

v8::Local<v8::String> v8String(v8::Isolate* isolate, const std::string& value)
{
    return v8::String::NewFromUtf8(isolate, value.c_str()).ToLocalChecked();
}

std::string stringFromV8(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    v8::String::Utf8Value utf8(isolate, value);
    return *utf8 ? std::string(*utf8, utf8.length()) : std::string();
}

void startRecordingApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    std::string categoryFilter = "*";
    std::string traceOptions = "record-until-full";
    if (info.Length() > 0 && info[0]->IsString())
        categoryFilter = stringFromV8(isolate, info[0]);
    if (info.Length() > 1 && info[1]->IsString())
        traceOptions = stringFromV8(isolate, info[1]);

    g_recordingCategory = firstEnabledCategoryFromFilter(categoryFilter);
    base::trace_event::TraceLog::GetInstance()->SetEnabled(
        base::trace_event::TraceConfig(categoryFilter, traceOptions),
        base::trace_event::TraceLog::RECORDING_MODE);
}

void stopRecordingApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    base::trace_event::TraceLog* traceLog = base::trace_event::TraceLog::GetInstance();
    traceLog->SetDisabled();

    std::string json;
    traceLog->Flush(base::BindRepeating([](std::string* out,
                                           const scoped_refptr<base::RefCountedString>& chunk,
                                           bool) {
        if (chunk)
            *out += chunk->as_string();
    }, &json));
    if (json.empty())
        json = "{\"traceEvents\":[]}";
    info.GetReturnValue().Set(v8String(info.GetIsolate(), json));
}

void getTraceBufferUsageApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    base::trace_event::TraceLogStatus status = base::trace_event::TraceLog::GetInstance()->GetStatus();
    double value = status.event_capacity ? static_cast<double>(status.event_count) / status.event_capacity : 0.0;

    v8::Local<v8::Object> result = v8::Object::New(isolate);
    result->Set(context, v8String(isolate, "value"), v8::Number::New(isolate, value)).ToChecked();
    result->Set(context, v8String(isolate, "percentage"), v8::Number::New(isolate, value * 100.0)).ToChecked();
    result->Set(context, v8String(isolate, "eventCount"), v8::Integer::NewFromUnsigned(isolate, status.event_count)).ToChecked();
    result->Set(context, v8String(isolate, "eventCapacity"), v8::Integer::NewFromUnsigned(isolate, status.event_capacity)).ToChecked();
    info.GetReturnValue().Set(result);
}

void recordInstantEventApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1 || !info[0]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "event name is required")));
        return;
    }

    const unsigned char* category = base::trace_event::TraceLog::GetCategoryGroupEnabled(g_recordingCategory.c_str());
    std::string name = stringFromV8(isolate, info[0]);
    std::string data;
    if (info.Length() > 1 && !info[1]->IsUndefined() && !info[1]->IsNull())
        data = stringFromV8(isolate, info[1]);
    base::trace_event::TraceArguments args = data.empty()
        ? base::trace_event::TraceArguments()
        : base::trace_event::TraceArguments("data", base::trace_event::TraceStringWithCopy(data.c_str()));
    base::trace_event::TraceLog::GetInstance()->AddTraceEvent(
        TRACE_EVENT_PHASE_INSTANT, category, name.c_str(), nullptr, 0, &args, TRACE_EVENT_SCOPE_PROCESS);
}

void initializeContentTracingApi(v8::Local<v8::Object> exports,
    v8::Local<v8::Value>,
    v8::Local<v8::Context> context,
    const NodeNative*)
{
    v8::Isolate* isolate = context->GetIsolate();
    exports->Set(context, v8String(isolate, "startRecording"),
        v8::FunctionTemplate::New(isolate, startRecordingApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "stopRecording"),
        v8::FunctionTemplate::New(isolate, stopRecordingApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "getTraceBufferUsage"),
        v8::FunctionTemplate::New(isolate, getTraceBufferUsageApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "recordInstantEvent"),
        v8::FunctionTemplate::New(isolate, recordInstantEventApi)->GetFunction(context).ToLocalChecked()).ToChecked();
}

const char ContentTracingScript[] = "exports = {};";
NodeNative nativeContentTracingNative { "ContentTracing", ContentTracingScript, sizeof(ContentTracingScript) - 1 };

} // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_common_content_tracing, initializeContentTracingApi, &nativeContentTracingNative)
