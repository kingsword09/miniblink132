#include "electron/common/TracingControllerImpl.h"

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/trace_event/perfetto_lightweight_backend.h"
#include "base/trace_event/trace_arguments.h"
#include "base/trace_event/trace_config.h"
#include "base/trace_event/trace_event_impl.h"
#include "base/trace_event/trace_log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace atom {
namespace {

class ConvertableAdapter final : public base::trace_event::ConvertableToTraceFormat {
public:
    explicit ConvertableAdapter(std::unique_ptr<v8::ConvertableToTraceFormat> value)
        : value_(std::move(value))
    {
    }

    void AppendAsTraceFormat(std::string* out) const override
    {
        if (value_)
            value_->AppendAsTraceFormat(out);
    }

private:
    std::unique_ptr<v8::ConvertableToTraceFormat> value_;
};

void FillBaseConvertables(int32_t num_args,
    const uint8_t* arg_types,
    std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables,
    std::array<std::unique_ptr<base::trace_event::ConvertableToTraceFormat>, base::trace_event::TraceArguments::kMaxSize>& out)
{
    const int32_t capped_args = std::min<int32_t>(num_args, base::trace_event::TraceArguments::kMaxSize);
    for (int32_t i = 0; i < capped_args; ++i) {
        if (arg_types && arg_types[i] == TRACE_VALUE_TYPE_CONVERTABLE && arg_convertables && arg_convertables[i])
            out[i] = std::make_unique<ConvertableAdapter>(std::move(arg_convertables[i]));
    }
}

uint64_t AddTraceEventToTraceLog(char phase,
    const uint8_t* category_enabled_flag,
    const char* name,
    const char* scope,
    uint64_t id,
    uint64_t bind_id,
    base::PlatformThreadId thread_id,
    const base::TimeTicks& timestamp,
    int32_t num_args,
    const char** arg_names,
    const uint8_t* arg_types,
    const uint64_t* arg_values,
    std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables,
    unsigned int flags)
{
    std::array<std::unique_ptr<base::trace_event::ConvertableToTraceFormat>, base::trace_event::TraceArguments::kMaxSize> base_convertables;
    FillBaseConvertables(num_args, arg_types, arg_convertables, base_convertables);

    base::trace_event::TraceArguments args(
        num_args,
        arg_names,
        arg_types,
        reinterpret_cast<const unsigned long long*>(arg_values),
        base_convertables.data());
    base::trace_event::TraceEventHandle handle =
        base::trace_event::TraceLog::GetInstance()->AddTraceEventWithThreadIdAndTimestamp(
            phase,
            category_enabled_flag,
            name,
            scope,
            id,
            bind_id,
            thread_id,
            timestamp,
            &args,
            flags);

    uint64_t result = 0;
    static_assert(sizeof(handle) <= sizeof(result), "trace handle must fit in V8 handle");
    memcpy(&result, &handle, sizeof(handle));
    return result;
}

} // namespace

extern "C" bool electronTracingControllerBridgeSmokeForTesting(v8::TracingController* controller)
{
    if (!controller)
        return false;

    base::trace_event::TraceLog* trace_log = base::trace_event::TraceLog::GetInstance();
    trace_log->SetEnabled(
        base::trace_event::TraceConfig("node,v8,electron,miniblink", "record-continuously"),
        base::trace_event::TraceLog::RECORDING_MODE);

    const uint8_t* category = controller->GetCategoryGroupEnabled("node");
    if (!category || !*category) {
        trace_log->SetDisabled();
        return false;
    }

    const char* arg_names[] = { "data" };
    const uint8_t arg_types[] = { TRACE_VALUE_TYPE_COPY_STRING };
    const char* data = "{\"ok\":true}";
    const uint64_t arg_values[] = { reinterpret_cast<uint64_t>(data) };

    uint64_t handle = controller->AddTraceEvent(
        TRACE_EVENT_PHASE_INSTANT,
        category,
        "electron-tracing-controller-bridge-smoke",
        nullptr,
        0,
        0,
        1,
        arg_names,
        arg_types,
        arg_values,
        nullptr,
        TRACE_EVENT_FLAG_COPY | TRACE_EVENT_SCOPE_PROCESS);

    trace_log->SetDisabled();

    std::string json;
    trace_log->Flush(base::BindRepeating([](std::string* out,
                                            const scoped_refptr<base::RefCountedString>& chunk,
                                            bool) {
        if (chunk)
            *out += chunk->as_string();
    }, &json));

    base::trace_event::PerfettoLightweightStats stats =
        base::trace_event::GetPerfettoLightweightStats();
    return handle != 0
        && json.find("electron-tracing-controller-bridge-smoke") != std::string::npos
        && json.find("\"data\":\"{\\\"ok\\\":true}\"") != std::string::npos
        && stats.initialized
        && !stats.recording
        && stats.event_count >= 1
        && stats.last_trace_size > 0
        && stats.last_trace_stats_success
        && stats.trace_packet_count >= 1
        && stats.track_event_packet_count >= 1
        && stats.track_event_instant_count >= 1
        && stats.track_event_named_count >= 1
        && stats.last_service_state_success
        && stats.last_service_state_size > 0
        && stats.service_state_data_source_count >= 1
        && stats.service_state_num_sessions_started >= 1;
}

class TracingControllerImpl::TraceLogObserver final : public base::trace_event::TraceLog::EnabledStateObserver {
public:
    explicit TraceLogObserver(TracingControllerImpl* owner)
        : owner_(owner)
    {
    }

    void OnTraceLogEnabled() override
    {
        if (owner_)
            owner_->NotifyTraceEnabled();
    }

    void OnTraceLogDisabled() override
    {
        if (owner_)
            owner_->NotifyTraceDisabled();
    }

private:
    TracingControllerImpl* owner_;
};

TracingControllerImpl::TracingControllerImpl()
    : observers_lock_(std::make_unique<base::Lock>())
    , trace_log_observer_(std::make_unique<TraceLogObserver>(this))
{
    base::trace_event::TraceLog::GetInstance()->AddEnabledStateObserver(trace_log_observer_.get());
}

TracingControllerImpl::~TracingControllerImpl()
{
    base::trace_event::TraceLog::GetInstance()->RemoveEnabledStateObserver(trace_log_observer_.get());
}

const uint8_t* TracingControllerImpl::GetCategoryGroupEnabled(const char* name)
{
    return base::trace_event::TraceLog::GetCategoryGroupEnabled(name);
}

uint64_t TracingControllerImpl::AddTraceEvent(char phase,
    const uint8_t* category_enabled_flag,
    const char* name,
    const char* scope,
    uint64_t id,
    uint64_t bind_id,
    int32_t num_args,
    const char** arg_names,
    const uint8_t* arg_types,
    const uint64_t* arg_values,
    std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables,
    unsigned int flags)
{
    return AddTraceEventToTraceLog(phase,
        category_enabled_flag,
        name,
        scope,
        id,
        bind_id,
        base::PlatformThread::CurrentId(),
        base::TimeTicks::Now(),
        num_args,
        arg_names,
        arg_types,
        arg_values,
        arg_convertables,
        flags);
}

uint64_t TracingControllerImpl::AddTraceEventWithTimestamp(char phase,
    const uint8_t* category_enabled_flag,
    const char* name,
    const char* scope,
    uint64_t id,
    uint64_t bind_id,
    int32_t num_args,
    const char** arg_names,
    const uint8_t* arg_types,
    const uint64_t* arg_values,
    std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables,
    unsigned int flags,
    int64_t timestamp_microseconds)
{
    return AddTraceEventToTraceLog(phase,
        category_enabled_flag,
        name,
        scope,
        id,
        bind_id,
        base::PlatformThread::CurrentId(),
        base::TimeTicks::FromInternalValue(timestamp_microseconds),
        num_args,
        arg_names,
        arg_types,
        arg_values,
        arg_convertables,
        flags);
}

void TracingControllerImpl::UpdateTraceEventDuration(const uint8_t* category_enabled_flag,
    const char* name,
    uint64_t handle)
{
    base::trace_event::TraceEventHandle trace_event_handle {};
    static_assert(sizeof(trace_event_handle) <= sizeof(handle), "trace handle must fit in V8 handle");
    memcpy(&trace_event_handle, &handle, sizeof(trace_event_handle));
    base::trace_event::TraceLog::GetInstance()->UpdateTraceEventDuration(category_enabled_flag, name, trace_event_handle);
}

void TracingControllerImpl::AddTraceStateObserver(TraceStateObserver* observer)
{
    if (!observer)
        return;

    base::AutoLock lock(*observers_lock_);
    if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end())
        observers_.push_back(observer);
}

void TracingControllerImpl::RemoveTraceStateObserver(TraceStateObserver* observer)
{
    base::AutoLock lock(*observers_lock_);
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer), observers_.end());
}

std::vector<TracingControllerImpl::TraceStateObserver*> TracingControllerImpl::CopyObservers()
{
    base::AutoLock lock(*observers_lock_);
    return observers_;
}

void TracingControllerImpl::NotifyTraceEnabled()
{
    std::vector<TraceStateObserver*> observers = CopyObservers();
    for (TraceStateObserver* observer : observers) {
        if (observer)
            observer->OnTraceEnabled();
    }
}

void TracingControllerImpl::NotifyTraceDisabled()
{
    std::vector<TraceStateObserver*> observers = CopyObservers();
    for (TraceStateObserver* observer : observers) {
        if (observer)
            observer->OnTraceDisabled();
    }
}

} // namespace atom
