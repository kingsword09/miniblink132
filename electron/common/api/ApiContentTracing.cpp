#include "electron/nodeblink.h"
#include "electron/common/NodeRegisterHelp.h"
#include "third_party/libnode/src/node_binding.h"
#include "v8/include/v8.h"

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/process/process_handle.h"
#include "base/sampling_heap_profiler/sampling_heap_profiler.h"
#include "base/time/time.h"
#include "base/trace_event/perfetto_lightweight_backend.h"
#include "base/trace_event/trace_config.h"
#include "base/trace_event/trace_event_lightweight.h"
#include "base/trace_event/trace_log.h"

#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string g_recordingCategory = "electron,miniblink";

struct HeapProfilingSessionOptions {
    bool enabled = false;
    bool memory_infra_category_enabled = false;
    bool running = false;
    uint32_t profile_id = 0;
    size_t sampling_rate = 100000;
    std::string mode = "all";
    std::string stack_mode = "native";
};

HeapProfilingSessionOptions g_heapProfiling;

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

bool objectProperty(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    v8::Local<v8::Object> object,
    const char* name,
    v8::Local<v8::Value>* out)
{
    v8::Local<v8::Value> value;
    if (!object->Get(context, v8String(isolate, name)).ToLocal(&value))
        return false;
    if (value->IsUndefined() || value->IsNull())
        return false;
    *out = value;
    return true;
}

bool objectStringProperty(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    v8::Local<v8::Object> object,
    const char* name,
    std::string* out)
{
    v8::Local<v8::Value> value;
    if (!objectProperty(isolate, context, object, name, &value) || !value->IsString())
        return false;
    *out = stringFromV8(isolate, value);
    return true;
}

bool objectBoolProperty(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    v8::Local<v8::Object> object,
    const char* name,
    bool* out)
{
    v8::Local<v8::Value> value;
    if (!objectProperty(isolate, context, object, name, &value) || !value->IsBoolean())
        return false;
    *out = value.As<v8::Boolean>()->Value();
    return true;
}

bool objectIntegerProperty(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    v8::Local<v8::Object> object,
    const char* name,
    size_t* out)
{
    v8::Local<v8::Value> value;
    if (!objectProperty(isolate, context, object, name, &value) || !value->IsNumber())
        return false;
    double number = value->NumberValue(context).FromMaybe(0.0);
    if (number < 0)
        return false;
    *out = static_cast<size_t>(number);
    return true;
}

bool categoryFilterIncludesMemoryInfra(const std::string& categoryFilter)
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
            if (token == "disabled-by-default-memory-infra" || token == "memory-infra")
                return true;
        }

        if (tokenEnd == categoryFilter.size())
            break;
        tokenStart = tokenEnd + 1;
    }
    return false;
}

HeapProfilingSessionOptions heapProfilingOptionsFromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    v8::Local<v8::Value> value,
    const std::string& categoryFilter)
{
    HeapProfilingSessionOptions options;
    options.memory_infra_category_enabled = categoryFilterIncludesMemoryInfra(categoryFilter);
    if (value.IsEmpty() || !value->IsObject())
        return options;

    v8::Local<v8::Object> object = value.As<v8::Object>();
    bool enabled = false;
    if (!objectBoolProperty(isolate, context, object, "enabled", &enabled) || !enabled)
        return options;

    options.enabled = true;
    objectStringProperty(isolate, context, object, "mode", &options.mode);
    objectStringProperty(isolate, context, object, "stackMode", &options.stack_mode);
    objectIntegerProperty(isolate, context, object, "samplingRate", &options.sampling_rate);
    return options;
}

std::string jsonEscape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                static constexpr char kHex[] = "0123456789abcdef";
                unsigned char byte = static_cast<unsigned char>(ch);
                escaped += "\\u00";
                escaped.push_back(kHex[(byte >> 4) & 0xf]);
                escaped.push_back(kHex[byte & 0xf]);
            } else {
                escaped.push_back(ch);
            }
            break;
        }
    }
    return escaped;
}

std::string jsonString(const std::string& value)
{
    return "\"" + jsonEscape(value) + "\"";
}

std::string pointerHex(const void* pointer)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << reinterpret_cast<uintptr_t>(pointer);
    return stream.str();
}

int64_t traceTimestampMicros()
{
    return base::TimeTicks::Now().since_origin().InMicroseconds();
}

std::string heapProfilingStatusTraceEvent(size_t sampleCount)
{
    std::ostringstream event;
    event << "{\"cat\":\"disabled-by-default-memory-infra\","
          << "\"name\":\"HeapProfiler.session\","
          << "\"ph\":\"i\","
          << "\"s\":\"p\","
          << "\"pid\":" << static_cast<int>(base::GetCurrentProcId()) << ","
          << "\"tid\":0,"
          << "\"ts\":" << traceTimestampMicros() << ","
          << "\"args\":{"
          << "\"enabled\":" << (g_heapProfiling.enabled ? "true" : "false") << ","
          << "\"active\":" << (g_heapProfiling.running ? "true" : "false") << ","
          << "\"memoryInfraCategoryEnabled\":" << (g_heapProfiling.memory_infra_category_enabled ? "true" : "false") << ","
          << "\"mode\":" << jsonString(g_heapProfiling.mode) << ","
          << "\"samplingRate\":" << static_cast<unsigned long long>(g_heapProfiling.sampling_rate) << ","
          << "\"stackMode\":" << jsonString(g_heapProfiling.stack_mode) << ","
          << "\"sampleCount\":" << static_cast<unsigned long long>(sampleCount)
          << "}}";
    return event.str();
}

std::string heapSampleTraceEvent(const base::SamplingHeapProfiler::Sample& sample)
{
    std::ostringstream event;
    event << "{\"cat\":\"disabled-by-default-memory-infra\","
          << "\"name\":\"HeapProfiler.sample\","
          << "\"ph\":\"i\","
          << "\"s\":\"p\","
          << "\"pid\":" << static_cast<int>(base::GetCurrentProcId()) << ","
          << "\"tid\":0,"
          << "\"ts\":" << traceTimestampMicros() << ","
          << "\"args\":{"
          << "\"size\":" << static_cast<unsigned long long>(sample.size) << ","
          << "\"total\":" << static_cast<unsigned long long>(sample.total) << ","
          << "\"allocator\":" << static_cast<int>(sample.allocator);
    if (sample.context)
        event << ",\"context\":" << jsonString(sample.context);
    if (sample.thread_name)
        event << ",\"threadName\":" << jsonString(sample.thread_name);
    event << ",\"stack\":[";
    for (size_t i = 0; i < sample.stack.size(); ++i) {
        if (i)
            event << ",";
        event << jsonString(pointerHex(sample.stack[i]));
    }
    event << "]}}";
    return event.str();
}

void appendTraceEventJson(std::string* json, const std::string& event)
{
    if (!json || event.empty())
        return;

    size_t arrayEnd = json->rfind(']');
    size_t arrayStart = json->find('[');
    if (arrayStart == std::string::npos || arrayEnd == std::string::npos || arrayStart > arrayEnd)
        return;

    bool emptyArray = true;
    for (size_t i = arrayStart + 1; i < arrayEnd; ++i) {
        if (!isspace(static_cast<unsigned char>((*json)[i]))) {
            emptyArray = false;
            break;
        }
    }
    json->insert(arrayEnd, (emptyArray ? "" : ",") + event);
}

void startHeapProfiling(const HeapProfilingSessionOptions& options)
{
    if (g_heapProfiling.running) {
        base::SamplingHeapProfiler::Get()->Stop();
        g_heapProfiling.running = false;
        g_heapProfiling.profile_id = 0;
    }

    g_heapProfiling = options;
    if (!g_heapProfiling.enabled || !g_heapProfiling.memory_infra_category_enabled)
        return;

    base::SamplingHeapProfiler::Init();
    base::SamplingHeapProfiler* profiler = base::SamplingHeapProfiler::Get();
    profiler->SetSamplingInterval(g_heapProfiling.sampling_rate);
    if (g_heapProfiling.stack_mode == "native-with-thread-names")
        profiler->EnableRecordThreadNames();

    uint32_t profileId = profiler->Start();
    if (!profileId)
        return;

    g_heapProfiling.profile_id = profileId;
    g_heapProfiling.running = true;
}

std::vector<base::SamplingHeapProfiler::Sample> stopHeapProfiling()
{
    std::vector<base::SamplingHeapProfiler::Sample> samples;
    if (!g_heapProfiling.running)
        return samples;

    base::SamplingHeapProfiler* profiler = base::SamplingHeapProfiler::Get();
    samples = profiler->GetSamples(g_heapProfiling.profile_id);
    profiler->Stop();
    g_heapProfiling.running = false;
    g_heapProfiling.profile_id = 0;
    return samples;
}

void appendHeapProfilingTraceEvents(std::string* json)
{
    if (!g_heapProfiling.enabled)
        return;

    std::vector<base::SamplingHeapProfiler::Sample> samples = stopHeapProfiling();
    appendTraceEventJson(json, heapProfilingStatusTraceEvent(samples.size()));
    for (const auto& sample : samples)
        appendTraceEventJson(json, heapSampleTraceEvent(sample));
}

std::string base64Encode(const std::string& data)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        unsigned int value = static_cast<unsigned char>(data[i]) << 16;
        if (i + 1 < data.size())
            value |= static_cast<unsigned char>(data[i + 1]) << 8;
        if (i + 2 < data.size())
            value |= static_cast<unsigned char>(data[i + 2]);

        encoded.push_back(kAlphabet[(value >> 18) & 0x3f]);
        encoded.push_back(kAlphabet[(value >> 12) & 0x3f]);
        encoded.push_back(i + 1 < data.size() ? kAlphabet[(value >> 6) & 0x3f] : '=');
        encoded.push_back(i + 2 < data.size() ? kAlphabet[value & 0x3f] : '=');
    }
    return encoded;
}

void startRecordingApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string categoryFilter = "*";
    std::string traceOptions = "record-until-full";
    if (info.Length() > 0 && info[0]->IsString())
        categoryFilter = stringFromV8(isolate, info[0]);
    if (info.Length() > 1 && info[1]->IsString())
        traceOptions = stringFromV8(isolate, info[1]);

    g_recordingCategory = firstEnabledCategoryFromFilter(categoryFilter);
    HeapProfilingSessionOptions heapOptions = heapProfilingOptionsFromV8(
        isolate,
        context,
        info.Length() > 2 ? info[2] : v8::Local<v8::Value>(),
        categoryFilter);
    base::trace_event::TraceConfig traceConfig(categoryFilter, traceOptions);
    if (heapOptions.enabled && heapOptions.memory_infra_category_enabled) {
        base::trace_event::TraceConfig::MemoryDumpConfig memoryDumpConfig;
        memoryDumpConfig.allowed_dump_modes.insert(base::trace_event::MemoryDumpLevelOfDetail::kBackground);
        memoryDumpConfig.allowed_dump_modes.insert(base::trace_event::MemoryDumpLevelOfDetail::kLight);
        memoryDumpConfig.allowed_dump_modes.insert(base::trace_event::MemoryDumpLevelOfDetail::kDetailed);
        base::trace_event::TraceConfig::MemoryDumpConfig::Trigger trigger;
        trigger.min_time_between_dumps_ms = 1000;
        trigger.level_of_detail = base::trace_event::MemoryDumpLevelOfDetail::kDetailed;
        trigger.trigger_type = base::trace_event::MemoryDumpType::kPeriodicInterval;
        memoryDumpConfig.triggers.push_back(trigger);
        traceConfig.ResetMemoryDumpConfig(memoryDumpConfig);
    }
    startHeapProfiling(heapOptions);
    base::trace_event::TraceLog::GetInstance()->SetEnabled(
        traceConfig,
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
    appendHeapProfilingTraceEvents(&json);
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

void getPerfettoStatsApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    base::trace_event::PerfettoLightweightStats stats = base::trace_event::GetPerfettoLightweightStats();

    v8::Local<v8::Object> result = v8::Object::New(isolate);
    result->Set(context, v8String(isolate, "initialized"), v8::Boolean::New(isolate, stats.initialized)).ToChecked();
    result->Set(context, v8String(isolate, "recording"), v8::Boolean::New(isolate, stats.recording)).ToChecked();
    result->Set(context, v8String(isolate, "startedCount"), v8::Number::New(isolate, static_cast<double>(stats.started_count))).ToChecked();
    result->Set(context, v8String(isolate, "eventCount"), v8::Number::New(isolate, static_cast<double>(stats.event_count))).ToChecked();
    result->Set(context, v8String(isolate, "droppedEventCount"), v8::Number::New(isolate, static_cast<double>(stats.dropped_event_count))).ToChecked();
    result->Set(context, v8String(isolate, "lastTraceSize"), v8::Number::New(isolate, static_cast<double>(stats.last_trace_size))).ToChecked();
    result->Set(context, v8String(isolate, "lastTracePacketCountHint"), v8::Number::New(isolate, static_cast<double>(stats.last_trace_packet_count_hint))).ToChecked();
    result->Set(context, v8String(isolate, "lastTraceStatsSuccess"), v8::Boolean::New(isolate, stats.last_trace_stats_success)).ToChecked();
    result->Set(context, v8String(isolate, "lastTraceStatsSize"), v8::Number::New(isolate, static_cast<double>(stats.last_trace_stats_size))).ToChecked();
    result->Set(context, v8String(isolate, "traceStatsProducersConnected"), v8::Number::New(isolate, static_cast<double>(stats.trace_stats_producers_connected))).ToChecked();
    result->Set(context, v8String(isolate, "traceStatsProducersSeen"), v8::Number::New(isolate, static_cast<double>(stats.trace_stats_producers_seen))).ToChecked();
    result->Set(context, v8String(isolate, "traceStatsDataSourcesRegistered"), v8::Number::New(isolate, static_cast<double>(stats.trace_stats_data_sources_registered))).ToChecked();
    result->Set(context, v8String(isolate, "traceStatsDataSourcesSeen"), v8::Number::New(isolate, static_cast<double>(stats.trace_stats_data_sources_seen))).ToChecked();
    result->Set(context, v8String(isolate, "traceStatsTracingSessions"), v8::Number::New(isolate, static_cast<double>(stats.trace_stats_tracing_sessions))).ToChecked();
    result->Set(context, v8String(isolate, "traceStatsTotalBuffers"), v8::Number::New(isolate, static_cast<double>(stats.trace_stats_total_buffers))).ToChecked();
    result->Set(context, v8String(isolate, "traceStatsBytesWritten"), v8::Number::New(isolate, static_cast<double>(stats.trace_stats_bytes_written))).ToChecked();
    result->Set(context, v8String(isolate, "traceStatsChunksWritten"), v8::Number::New(isolate, static_cast<double>(stats.trace_stats_chunks_written))).ToChecked();
    result->Set(context, v8String(isolate, "tracePacketCount"), v8::Number::New(isolate, static_cast<double>(stats.trace_packet_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackDescriptorPacketCount"), v8::Number::New(isolate, static_cast<double>(stats.track_descriptor_packet_count))).ToChecked();
    result->Set(context, v8String(isolate, "processDescriptorCount"), v8::Number::New(isolate, static_cast<double>(stats.process_descriptor_count))).ToChecked();
    result->Set(context, v8String(isolate, "threadDescriptorCount"), v8::Number::New(isolate, static_cast<double>(stats.thread_descriptor_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventPacketCount"), v8::Number::New(isolate, static_cast<double>(stats.track_event_packet_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventSliceBeginCount"), v8::Number::New(isolate, static_cast<double>(stats.track_event_slice_begin_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventSliceEndCount"), v8::Number::New(isolate, static_cast<double>(stats.track_event_slice_end_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventInstantCount"), v8::Number::New(isolate, static_cast<double>(stats.track_event_instant_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventCounterCount"), v8::Number::New(isolate, static_cast<double>(stats.track_event_counter_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventCounterValueCount"), v8::Number::New(isolate, static_cast<double>(stats.track_event_counter_value_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventNamedCount"), v8::Number::New(isolate, static_cast<double>(stats.track_event_named_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventDirectNameCount"), v8::Number::New(isolate, static_cast<double>(stats.track_event_direct_name_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventInternedNameCount"), v8::Number::New(isolate, static_cast<double>(stats.track_event_interned_name_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventTrackNameCount"), v8::Number::New(isolate, static_cast<double>(stats.track_event_track_name_count))).ToChecked();
    result->Set(context, v8String(isolate, "trackEventNames"), v8String(isolate, base::trace_event::GetPerfettoLightweightTrackEventNames())).ToChecked();
    result->Set(context, v8String(isolate, "lastServiceStateSuccess"), v8::Boolean::New(isolate, stats.last_service_state_success)).ToChecked();
    result->Set(context, v8String(isolate, "lastServiceStateSize"), v8::Number::New(isolate, static_cast<double>(stats.last_service_state_size))).ToChecked();
    result->Set(context, v8String(isolate, "serviceStateProducerCount"), v8::Number::New(isolate, static_cast<double>(stats.service_state_producer_count))).ToChecked();
    result->Set(context, v8String(isolate, "serviceStateDataSourceCount"), v8::Number::New(isolate, static_cast<double>(stats.service_state_data_source_count))).ToChecked();
    result->Set(context, v8String(isolate, "serviceStateTracingSessionCount"), v8::Number::New(isolate, static_cast<double>(stats.service_state_tracing_session_count))).ToChecked();
    result->Set(context, v8String(isolate, "serviceStateSupportsTracingSessions"), v8::Boolean::New(isolate, stats.service_state_supports_tracing_sessions)).ToChecked();
    result->Set(context, v8String(isolate, "serviceStateNumSessions"), v8::Number::New(isolate, static_cast<double>(stats.service_state_num_sessions))).ToChecked();
    result->Set(context, v8String(isolate, "serviceStateNumSessionsStarted"), v8::Number::New(isolate, static_cast<double>(stats.service_state_num_sessions_started))).ToChecked();
    info.GetReturnValue().Set(result);
}

void getPerfettoTraceDataApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    std::string traceData = base::trace_event::GetPerfettoLightweightTraceData();
    info.GetReturnValue().Set(v8String(info.GetIsolate(), base64Encode(traceData)));
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

void recordCounterApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsNumber()) {
        isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "counter name and numeric value are required")));
        return;
    }

    const unsigned char* category = base::trace_event::TraceLog::GetCategoryGroupEnabled(g_recordingCategory.c_str());
    std::string name = stringFromV8(isolate, info[0]);
    double value = info[1]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0.0);
    base::trace_event::TraceArguments args("value", value);
    base::trace_event::TraceLog::GetInstance()->AddTraceEvent(
        TRACE_EVENT_PHASE_COUNTER, category, name.c_str(), nullptr, 0, &args, TRACE_EVENT_FLAG_NONE);
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
    exports->Set(context, v8String(isolate, "getPerfettoStats"),
        v8::FunctionTemplate::New(isolate, getPerfettoStatsApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "getPerfettoTraceData"),
        v8::FunctionTemplate::New(isolate, getPerfettoTraceDataApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "recordInstantEvent"),
        v8::FunctionTemplate::New(isolate, recordInstantEventApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "recordCounter"),
        v8::FunctionTemplate::New(isolate, recordCounterApi)->GetFunction(context).ToLocalChecked()).ToChecked();
}

const char ContentTracingScript[] = "exports = {};";
NodeNative nativeContentTracingNative { "ContentTracing", ContentTracingScript, sizeof(ContentTracingScript) - 1 };

} // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_common_content_tracing, initializeContentTracingApi, &nativeContentTracingNative)
