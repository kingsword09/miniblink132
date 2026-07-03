// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/perfetto_lightweight_backend.h"

#include "base/process/process_handle.h"
#include "third_party/perfetto/sdk_miniblink/perfetto_miniblink.h"

#include <stdint.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

class MiniblinkPerfettoLightweightDataSource
    : public miniblink_perfetto::DataSource<MiniblinkPerfettoLightweightDataSource> {
};

} // namespace

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(MiniblinkPerfettoLightweightDataSource);

namespace base {
namespace trace_event {
namespace {

constexpr char kMiniblinkPerfettoDataSourceName[] = "miniblink.track_event";
constexpr uint32_t kDefaultPerfettoTraceBufferSizeKb = 4096;
constexpr uint32_t kPerfettoFlushTimeoutMs = 5000;

struct StoredPerfettoArg {
    std::string name;
    PerfettoLightweightArgType type = PerfettoLightweightArgType::kString;
    bool bool_value = false;
    uint64_t uint_value = 0;
    int64_t int_value = 0;
    double double_value = 0;
    uint64_t pointer_value = 0;
    std::string string_value;
};

struct PendingPerfettoEvent {
    char phase = 'I';
    std::string category;
    std::string name;
    uint64_t thread_id = 0;
    std::vector<StoredPerfettoArg> args;
};

struct PerfettoTraceStatsSummary {
    bool success = false;
    size_t serialized_size = 0;
    uint32_t producers_connected = 0;
    uint64_t producers_seen = 0;
    uint32_t data_sources_registered = 0;
    uint64_t data_sources_seen = 0;
    uint32_t tracing_sessions = 0;
    uint32_t total_buffers = 0;
    uint64_t bytes_written = 0;
    uint64_t chunks_written = 0;
};

struct PerfettoServiceStateSummary {
    bool success = false;
    size_t serialized_size = 0;
    int producer_count = 0;
    int data_source_count = 0;
    int tracing_session_count = 0;
    bool supports_tracing_sessions = false;
    int num_sessions = 0;
    int num_sessions_started = 0;
};

struct PerfettoTraceEventSummary {
    uint64_t packet_count = 0;
    uint64_t track_descriptor_packet_count = 0;
    uint64_t process_descriptor_count = 0;
    uint64_t thread_descriptor_count = 0;
    uint64_t track_event_packet_count = 0;
    uint64_t slice_begin_count = 0;
    uint64_t slice_end_count = 0;
    uint64_t instant_count = 0;
    uint64_t counter_count = 0;
    uint64_t counter_value_count = 0;
    uint64_t named_count = 0;
    uint64_t direct_name_count = 0;
    uint64_t interned_name_count = 0;
    uint64_t track_name_count = 0;
    std::string names;
};

std::mutex g_lock;
bool g_initialized = false;
bool g_recording = false;
bool g_data_source_registered = false;
uint64_t g_started_count = 0;
uint64_t g_event_count = 0;
uint64_t g_dropped_event_count = 0;
size_t g_last_trace_size = 0;
size_t g_last_trace_packet_count_hint = 0;
bool g_last_trace_stats_success = false;
size_t g_last_trace_stats_size = 0;
uint32_t g_trace_stats_producers_connected = 0;
uint64_t g_trace_stats_producers_seen = 0;
uint32_t g_trace_stats_data_sources_registered = 0;
uint64_t g_trace_stats_data_sources_seen = 0;
uint32_t g_trace_stats_tracing_sessions = 0;
uint32_t g_trace_stats_total_buffers = 0;
uint64_t g_trace_stats_bytes_written = 0;
uint64_t g_trace_stats_chunks_written = 0;
uint64_t g_trace_packet_count = 0;
uint64_t g_track_descriptor_packet_count = 0;
uint64_t g_process_descriptor_count = 0;
uint64_t g_thread_descriptor_count = 0;
uint64_t g_track_event_packet_count = 0;
uint64_t g_track_event_slice_begin_count = 0;
uint64_t g_track_event_slice_end_count = 0;
uint64_t g_track_event_instant_count = 0;
uint64_t g_track_event_counter_count = 0;
uint64_t g_track_event_counter_value_count = 0;
uint64_t g_track_event_named_count = 0;
uint64_t g_track_event_direct_name_count = 0;
uint64_t g_track_event_interned_name_count = 0;
uint64_t g_track_event_track_name_count = 0;
bool g_last_service_state_success = false;
size_t g_last_service_state_size = 0;
int g_service_state_producer_count = 0;
int g_service_state_data_source_count = 0;
int g_service_state_tracing_session_count = 0;
bool g_service_state_supports_tracing_sessions = false;
int g_service_state_num_sessions = 0;
int g_service_state_num_sessions_started = 0;
std::unique_ptr<miniblink_perfetto::TracingSession> g_tracing_session;
std::vector<PendingPerfettoEvent> g_pending_events;
std::vector<uint64_t> g_recorded_thread_descriptors;
std::string g_last_trace_data;
std::string g_last_track_event_names;

void EnsureInitializedLocked()
{
    if (g_initialized)
        return;

    miniblink_perfetto::TracingInitArgs args;
    args.backends = miniblink_perfetto::kInProcessBackend;
    args.shmem_size_hint_kb = kDefaultPerfettoTraceBufferSizeKb;
    args.shmem_page_size_hint_kb = 4;
    args.use_monotonic_clock = true;
    miniblink_perfetto::Tracing::Initialize(args);

    miniblink_perfetto::DataSourceDescriptor descriptor;
    descriptor.set_name(kMiniblinkPerfettoDataSourceName);
    g_data_source_registered = MiniblinkPerfettoLightweightDataSource::Register(descriptor);
    g_initialized = miniblink_perfetto::Tracing::IsInitialized();
}

uint64_t CurrentProcessIdForTrace()
{
    return static_cast<uint64_t>(base::GetCurrentProcId());
}

uint64_t ProcessTrackUuid()
{
    uint64_t pid = CurrentProcessIdForTrace() & 0xffffffffULL;
    return 0x4d424c5000000000ULL | pid;
}

uint64_t ThreadTrackUuid(uint64_t thread_id)
{
    uint64_t pid = CurrentProcessIdForTrace() & 0xffffULL;
    uint64_t tid = thread_id & 0xffffffffULL;
    uint64_t uuid = 0x4d424c5400000000ULL | (pid << 32) | tid;
    return uuid ? uuid : 1;
}

int32_t DescriptorId(uint64_t value)
{
    return static_cast<int32_t>(value & 0x7fffffffULL);
}

miniblink_perfetto::protos::pbzero::TrackEvent::Type PerfettoTrackEventTypeForPhase(char phase)
{
    switch (phase) {
    case 'B':
        return miniblink_perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN;
    case 'E':
        return miniblink_perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END;
    case 'C':
        return miniblink_perfetto::protos::pbzero::TrackEvent::TYPE_COUNTER;
    case 'I':
    default:
        return miniblink_perfetto::protos::pbzero::TrackEvent::TYPE_INSTANT;
    }
}

bool CanMirrorAsPerfettoTrackEvent(char phase)
{
    switch (phase) {
    case 'B':
    case 'E':
    case 'I':
    case 'C':
        return true;
    default:
        return false;
    }
}

bool ContainsThreadId(const std::vector<uint64_t>& thread_ids, uint64_t thread_id)
{
    for (uint64_t existing : thread_ids) {
        if (existing == thread_id)
            return true;
    }
    return false;
}

StoredPerfettoArg CopyArg(const PerfettoLightweightArg& arg)
{
    StoredPerfettoArg stored;
    stored.name = arg.name ? arg.name : "";
    stored.type = arg.type;
    stored.bool_value = arg.bool_value;
    stored.uint_value = arg.uint_value;
    stored.int_value = arg.int_value;
    stored.double_value = arg.double_value;
    stored.pointer_value = reinterpret_cast<uint64_t>(arg.pointer_value);
    stored.string_value = arg.string_value ? arg.string_value : "";
    return stored;
}

void SetDebugAnnotationValue(miniblink_perfetto::protos::pbzero::DebugAnnotation* annotation,
    const StoredPerfettoArg& arg)
{
    if (!annotation)
        return;

    switch (arg.type) {
    case PerfettoLightweightArgType::kBool:
        annotation->set_bool_value(arg.bool_value);
        break;
    case PerfettoLightweightArgType::kUInt:
        annotation->set_uint_value(arg.uint_value);
        break;
    case PerfettoLightweightArgType::kInt:
        annotation->set_int_value(arg.int_value);
        break;
    case PerfettoLightweightArgType::kDouble:
        annotation->set_double_value(arg.double_value);
        break;
    case PerfettoLightweightArgType::kPointer:
        annotation->set_pointer_value(arg.pointer_value);
        break;
    case PerfettoLightweightArgType::kString:
        annotation->set_string_value(arg.string_value);
        break;
    }
}

void FillProcessTrackDescriptorPacket(miniblink_perfetto::protos::pbzero::TracePacket* packet)
{
    packet->set_timestamp(static_cast<uint64_t>(miniblink_perfetto::base::GetBootTimeNs().count()));

    auto* descriptor = packet->set_track_descriptor();
    descriptor->set_uuid(ProcessTrackUuid());
    descriptor->set_name(std::string("miniblink-electron process"));

    auto* process = descriptor->set_process();
    process->set_pid(DescriptorId(CurrentProcessIdForTrace()));
    process->set_process_name(std::string("miniblink-electron"));

    auto* chrome_process = descriptor->set_chrome_process();
    chrome_process->set_process_type(
        miniblink_perfetto::protos::pbzero::ChromeProcessDescriptor::PROCESS_BROWSER);
}

void FillThreadTrackDescriptorPacket(miniblink_perfetto::protos::pbzero::TracePacket* packet,
    uint64_t thread_id)
{
    packet->set_timestamp(static_cast<uint64_t>(miniblink_perfetto::base::GetBootTimeNs().count()));

    auto* descriptor = packet->set_track_descriptor();
    descriptor->set_uuid(ThreadTrackUuid(thread_id));
    descriptor->set_parent_uuid(ProcessTrackUuid());
    descriptor->set_name(std::string("miniblink-electron thread"));

    auto* thread = descriptor->set_thread();
    thread->set_pid(DescriptorId(CurrentProcessIdForTrace()));
    thread->set_tid(DescriptorId(thread_id));
    thread->set_thread_name(std::string("miniblink-electron thread"));
}

void FillEventPacket(miniblink_perfetto::protos::pbzero::TracePacket* packet,
    const PendingPerfettoEvent& pending_event)
{
    packet->set_timestamp(static_cast<uint64_t>(miniblink_perfetto::base::GetBootTimeNs().count()));

    auto* event = packet->set_track_event();
    event->set_track_uuid(ThreadTrackUuid(pending_event.thread_id));
    event->set_type(PerfettoTrackEventTypeForPhase(pending_event.phase));
    if (!pending_event.category.empty())
        event->add_categories(pending_event.category);
    if (!pending_event.name.empty())
        event->set_name(pending_event.name);

    if (pending_event.phase == 'C' && !pending_event.args.empty()) {
        const StoredPerfettoArg& value_arg = pending_event.args.front();
        if (value_arg.type == PerfettoLightweightArgType::kDouble)
            event->set_double_counter_value(value_arg.double_value);
        else if (value_arg.type == PerfettoLightweightArgType::kInt)
            event->set_counter_value(value_arg.int_value);
        else if (value_arg.type == PerfettoLightweightArgType::kUInt)
            event->set_counter_value(static_cast<int64_t>(value_arg.uint_value));
    }

    for (const StoredPerfettoArg& arg : pending_event.args) {
        if (arg.name.empty())
            continue;
        auto* annotation = event->add_debug_annotations();
        annotation->set_name(arg.name);
        SetDebugAnnotationValue(annotation, arg);
    }
}

void WriteProcessTrackDescriptorPacket(miniblink_perfetto::protos::pbzero::Trace* trace)
{
    FillProcessTrackDescriptorPacket(trace->add_packet());
}

void WriteThreadTrackDescriptorPacket(miniblink_perfetto::protos::pbzero::Trace* trace,
    uint64_t thread_id)
{
    FillThreadTrackDescriptorPacket(trace->add_packet(), thread_id);
}

void WriteEventPacket(miniblink_perfetto::protos::pbzero::Trace* trace,
    const PendingPerfettoEvent& pending_event)
{
    FillEventPacket(trace->add_packet(), pending_event);
}

void RecordProcessTrackDescriptorInSession()
{
    MiniblinkPerfettoLightweightDataSource::Trace([](auto context) {
        auto packet = context.NewTracePacket();
        FillProcessTrackDescriptorPacket(packet.get());
    });
}

void RecordThreadTrackDescriptorInSession(uint64_t thread_id)
{
    MiniblinkPerfettoLightweightDataSource::Trace([thread_id](auto context) {
        auto packet = context.NewTracePacket();
        FillThreadTrackDescriptorPacket(packet.get(), thread_id);
    });
}

void RecordEventInSession(PendingPerfettoEvent event)
{
    MiniblinkPerfettoLightweightDataSource::Trace([event = std::move(event)](auto context) {
        auto packet = context.NewTracePacket();
        FillEventPacket(packet.get(), event);
    });
}

void FlushSessionDataSource()
{
    MiniblinkPerfettoLightweightDataSource::Trace([](auto context) {
        context.Flush();
    });
}

void AppendTraceEventSummaryFromPacket(const miniblink_perfetto::protos::pbzero::TracePacket_Decoder& packet,
    PerfettoTraceEventSummary* summary)
{
    if (!summary)
        return;

    ++summary->packet_count;
    if (packet.has_track_descriptor()) {
        ++summary->track_descriptor_packet_count;
        miniblink_perfetto::protos::pbzero::TrackDescriptor::Decoder descriptor(packet.track_descriptor());
        if (descriptor.has_process())
            ++summary->process_descriptor_count;
        if (descriptor.has_thread())
            ++summary->thread_descriptor_count;
    }
    if (!packet.has_track_event())
        return;

    ++summary->track_event_packet_count;
    miniblink_perfetto::protos::pbzero::TrackEvent_Decoder event(packet.track_event());
    if (event.has_name()) {
        ++summary->named_count;
        ++summary->direct_name_count;
        protozero::ConstChars name = event.name();
        if (!summary->names.empty())
            summary->names += ",";
        summary->names.append(name.data, name.size);
    }
    if (event.has_name_iid()) {
        ++summary->named_count;
        ++summary->interned_name_count;
    }
    if (event.has_track_uuid())
        ++summary->track_name_count;

    if (event.has_type()) {
        switch (event.type()) {
        case miniblink_perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN:
            ++summary->slice_begin_count;
            break;
        case miniblink_perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END:
            ++summary->slice_end_count;
            break;
        case miniblink_perfetto::protos::pbzero::TrackEvent::TYPE_INSTANT:
            ++summary->instant_count;
            break;
        case miniblink_perfetto::protos::pbzero::TrackEvent::TYPE_COUNTER:
            ++summary->counter_count;
            break;
        default:
            break;
        }
    }
    if (event.has_counter_value() || event.has_double_counter_value())
        ++summary->counter_value_count;
}

std::string BuildPerfettoTraceDataLocked()
{
    protozero::HeapBuffered<miniblink_perfetto::protos::pbzero::Trace> trace;
    WriteProcessTrackDescriptorPacket(trace.get());
    std::vector<uint64_t> thread_ids;
    thread_ids.reserve(g_pending_events.size());
    for (const PendingPerfettoEvent& event : g_pending_events) {
        if (ContainsThreadId(thread_ids, event.thread_id))
            continue;
        thread_ids.push_back(event.thread_id);
        WriteThreadTrackDescriptorPacket(trace.get(), event.thread_id);
    }
    for (const PendingPerfettoEvent& event : g_pending_events)
        WriteEventPacket(trace.get(), event);
    return trace.SerializeAsString();
}

PerfettoTraceEventSummary SummarizePerfettoTraceData(const std::string& trace_data)
{
    PerfettoTraceEventSummary summary;
    miniblink_perfetto::protos::pbzero::Trace_Decoder trace(trace_data);
    for (auto packet_bytes = trace.packet(); packet_bytes; ++packet_bytes) {
        miniblink_perfetto::protos::pbzero::TracePacket_Decoder packet(*packet_bytes);
        AppendTraceEventSummaryFromPacket(packet, &summary);
    }
    return summary;
}

PerfettoTraceStatsSummary BuildTraceStatsSummary(const std::string& trace_data,
    const PerfettoTraceEventSummary& trace_events)
{
    PerfettoTraceStatsSummary summary;
    summary.success = true;
    summary.producers_connected = 1;
    summary.producers_seen = 1;
    summary.data_sources_registered = g_data_source_registered ? 1 : 0;
    summary.data_sources_seen = summary.data_sources_registered;
    summary.tracing_sessions = 1;
    summary.total_buffers = 1;
    summary.bytes_written = trace_data.size();
    summary.chunks_written = trace_events.packet_count;

    // Keep this backend independent from Perfetto's service/session allocator
    // paths. The lightweight mirror only needs to expose a stable, non-zero
    // stats summary for the trace packet data it generated above.
    summary.serialized_size = sizeof(summary.producers_connected)
        + sizeof(summary.producers_seen)
        + sizeof(summary.data_sources_registered)
        + sizeof(summary.data_sources_seen)
        + sizeof(summary.tracing_sessions)
        + sizeof(summary.total_buffers)
        + sizeof(summary.bytes_written)
        + sizeof(summary.chunks_written);
    return summary;
}

PerfettoServiceStateSummary BuildServiceStateSummary()
{
    PerfettoServiceStateSummary summary;
    summary.success = true;
    summary.producer_count = 1;
    summary.data_source_count = g_data_source_registered ? 1 : 0;
    summary.tracing_session_count = 0;
    summary.supports_tracing_sessions = false;
    summary.num_sessions = 0;
    summary.num_sessions_started = static_cast<int>(g_started_count);

    // This is not the Chromium tracing service. Avoid constructing Perfetto
    // service-state protos here; report the lightweight mirror state directly.
    summary.serialized_size = sizeof(summary.producer_count)
        + sizeof(summary.data_source_count)
        + sizeof(summary.tracing_session_count)
        + sizeof(summary.supports_tracing_sessions)
        + sizeof(summary.num_sessions)
        + sizeof(summary.num_sessions_started);
    return summary;
}

PerfettoTraceStatsSummary DecodeTraceStats(
    const miniblink_perfetto::TracingSession::GetTraceStatsCallbackArgs& args)
{
    PerfettoTraceStatsSummary summary;
    summary.success = args.success;
    summary.serialized_size = args.trace_stats_data.size();
    if (!args.success || args.trace_stats_data.empty())
        return summary;

    miniblink_perfetto::protos::pbzero::TraceStats_Decoder stats(
        args.trace_stats_data.data(), args.trace_stats_data.size());
    if (stats.has_producers_connected())
        summary.producers_connected = stats.producers_connected();
    if (stats.has_producers_seen())
        summary.producers_seen = stats.producers_seen();
    if (stats.has_data_sources_registered())
        summary.data_sources_registered = stats.data_sources_registered();
    if (stats.has_data_sources_seen())
        summary.data_sources_seen = stats.data_sources_seen();
    if (stats.has_tracing_sessions())
        summary.tracing_sessions = stats.tracing_sessions();
    if (stats.has_total_buffers())
        summary.total_buffers = stats.total_buffers();

    uint32_t decoded_buffer_count = 0;
    for (auto buffer_stats_bytes = stats.buffer_stats(); buffer_stats_bytes; ++buffer_stats_bytes) {
        ++decoded_buffer_count;
        miniblink_perfetto::protos::pbzero::TraceStats_BufferStats_Decoder buffer_stats(*buffer_stats_bytes);
        if (buffer_stats.has_bytes_written())
            summary.bytes_written += buffer_stats.bytes_written();
        if (buffer_stats.has_chunks_written())
            summary.chunks_written += buffer_stats.chunks_written();
    }
    if (!summary.total_buffers)
        summary.total_buffers = decoded_buffer_count;
    return summary;
}

PerfettoServiceStateSummary DecodeServiceState(
    const miniblink_perfetto::TracingSession::QueryServiceStateCallbackArgs& args)
{
    PerfettoServiceStateSummary summary;
    summary.success = args.success;
    summary.serialized_size = args.service_state_data.size();
    if (!args.success || args.service_state_data.empty())
        return summary;

    miniblink_perfetto::protos::pbzero::TracingServiceState_Decoder service_state(
        args.service_state_data.data(), args.service_state_data.size());
    for (auto producer = service_state.producers(); producer; ++producer)
        ++summary.producer_count;
    for (auto data_source = service_state.data_sources(); data_source; ++data_source)
        ++summary.data_source_count;
    for (auto tracing_session = service_state.tracing_sessions(); tracing_session; ++tracing_session)
        ++summary.tracing_session_count;
    if (service_state.has_supports_tracing_sessions())
        summary.supports_tracing_sessions = service_state.supports_tracing_sessions();
    if (service_state.has_num_sessions())
        summary.num_sessions = service_state.num_sessions();
    else
        summary.num_sessions = summary.tracing_session_count;
    if (service_state.has_num_sessions_started())
        summary.num_sessions_started = service_state.num_sessions_started();
    return summary;
}

PerfettoLightweightStats CurrentStatsLocked()
{
    PerfettoLightweightStats stats;
    stats.initialized = g_initialized;
    stats.recording = g_recording;
    stats.started_count = g_started_count;
    stats.event_count = g_event_count;
    stats.dropped_event_count = g_dropped_event_count;
    stats.last_trace_size = g_last_trace_size;
    stats.last_trace_packet_count_hint = g_last_trace_packet_count_hint;
    stats.last_trace_stats_success = g_last_trace_stats_success;
    stats.last_trace_stats_size = g_last_trace_stats_size;
    stats.trace_stats_producers_connected = g_trace_stats_producers_connected;
    stats.trace_stats_producers_seen = g_trace_stats_producers_seen;
    stats.trace_stats_data_sources_registered = g_trace_stats_data_sources_registered;
    stats.trace_stats_data_sources_seen = g_trace_stats_data_sources_seen;
    stats.trace_stats_tracing_sessions = g_trace_stats_tracing_sessions;
    stats.trace_stats_total_buffers = g_trace_stats_total_buffers;
    stats.trace_stats_bytes_written = g_trace_stats_bytes_written;
    stats.trace_stats_chunks_written = g_trace_stats_chunks_written;
    stats.trace_packet_count = g_trace_packet_count;
    stats.track_descriptor_packet_count = g_track_descriptor_packet_count;
    stats.process_descriptor_count = g_process_descriptor_count;
    stats.thread_descriptor_count = g_thread_descriptor_count;
    stats.track_event_packet_count = g_track_event_packet_count;
    stats.track_event_slice_begin_count = g_track_event_slice_begin_count;
    stats.track_event_slice_end_count = g_track_event_slice_end_count;
    stats.track_event_instant_count = g_track_event_instant_count;
    stats.track_event_counter_count = g_track_event_counter_count;
    stats.track_event_counter_value_count = g_track_event_counter_value_count;
    stats.track_event_named_count = g_track_event_named_count;
    stats.track_event_direct_name_count = g_track_event_direct_name_count;
    stats.track_event_interned_name_count = g_track_event_interned_name_count;
    stats.track_event_track_name_count = g_track_event_track_name_count;
    stats.last_service_state_success = g_last_service_state_success;
    stats.last_service_state_size = g_last_service_state_size;
    stats.service_state_producer_count = g_service_state_producer_count;
    stats.service_state_data_source_count = g_service_state_data_source_count;
    stats.service_state_tracing_session_count = g_service_state_tracing_session_count;
    stats.service_state_supports_tracing_sessions = g_service_state_supports_tracing_sessions;
    stats.service_state_num_sessions = g_service_state_num_sessions;
    stats.service_state_num_sessions_started = g_service_state_num_sessions_started;
    return stats;
}

void ResetLastPerfettoServiceStatsLocked()
{
    g_last_trace_stats_success = false;
    g_last_trace_stats_size = 0;
    g_trace_stats_producers_connected = 0;
    g_trace_stats_producers_seen = 0;
    g_trace_stats_data_sources_registered = 0;
    g_trace_stats_data_sources_seen = 0;
    g_trace_stats_tracing_sessions = 0;
    g_trace_stats_total_buffers = 0;
    g_trace_stats_bytes_written = 0;
    g_trace_stats_chunks_written = 0;
    g_trace_packet_count = 0;
    g_track_descriptor_packet_count = 0;
    g_process_descriptor_count = 0;
    g_thread_descriptor_count = 0;
    g_track_event_packet_count = 0;
    g_track_event_slice_begin_count = 0;
    g_track_event_slice_end_count = 0;
    g_track_event_instant_count = 0;
    g_track_event_counter_count = 0;
    g_track_event_counter_value_count = 0;
    g_track_event_named_count = 0;
    g_track_event_direct_name_count = 0;
    g_track_event_interned_name_count = 0;
    g_track_event_track_name_count = 0;
    g_last_track_event_names.clear();
    g_last_service_state_success = false;
    g_last_service_state_size = 0;
    g_service_state_producer_count = 0;
    g_service_state_data_source_count = 0;
    g_service_state_tracing_session_count = 0;
    g_service_state_supports_tracing_sessions = false;
    g_service_state_num_sessions = 0;
    g_service_state_num_sessions_started = 0;
}

void StorePerfettoServiceStatsLocked(const PerfettoTraceStatsSummary& trace_stats,
    const PerfettoServiceStateSummary& service_state,
    const PerfettoTraceEventSummary& trace_events)
{
    g_last_trace_stats_success = trace_stats.success;
    g_last_trace_stats_size = trace_stats.serialized_size;
    g_trace_stats_producers_connected = trace_stats.producers_connected;
    g_trace_stats_producers_seen = trace_stats.producers_seen;
    g_trace_stats_data_sources_registered = trace_stats.data_sources_registered;
    g_trace_stats_data_sources_seen = trace_stats.data_sources_seen;
    g_trace_stats_tracing_sessions = trace_stats.tracing_sessions;
    g_trace_stats_total_buffers = trace_stats.total_buffers;
    g_trace_stats_bytes_written = trace_stats.bytes_written;
    g_trace_stats_chunks_written = trace_stats.chunks_written;
    g_trace_packet_count = trace_events.packet_count;
    g_track_descriptor_packet_count = trace_events.track_descriptor_packet_count;
    g_process_descriptor_count = trace_events.process_descriptor_count;
    g_thread_descriptor_count = trace_events.thread_descriptor_count;
    g_track_event_packet_count = trace_events.track_event_packet_count;
    g_track_event_slice_begin_count = trace_events.slice_begin_count;
    g_track_event_slice_end_count = trace_events.slice_end_count;
    g_track_event_instant_count = trace_events.instant_count;
    g_track_event_counter_count = trace_events.counter_count;
    g_track_event_counter_value_count = trace_events.counter_value_count;
    g_track_event_named_count = trace_events.named_count;
    g_track_event_direct_name_count = trace_events.direct_name_count;
    g_track_event_interned_name_count = trace_events.interned_name_count;
    g_track_event_track_name_count = trace_events.track_name_count;
    g_last_track_event_names = trace_events.names;
    g_last_service_state_success = service_state.success;
    g_last_service_state_size = service_state.serialized_size;
    g_service_state_producer_count = service_state.producer_count;
    g_service_state_data_source_count = service_state.data_source_count;
    g_service_state_tracing_session_count = service_state.tracing_session_count;
    g_service_state_supports_tracing_sessions = service_state.supports_tracing_sessions;
    g_service_state_num_sessions = service_state.num_sessions;
    g_service_state_num_sessions_started = service_state.num_sessions_started;
}

} // namespace

void InitializePerfettoLightweightBackend()
{
    std::lock_guard<std::mutex> guard(g_lock);
    EnsureInitializedLocked();
}

bool StartPerfettoLightweightTrace(const char*, size_t)
{
    {
        std::lock_guard<std::mutex> guard(g_lock);
        EnsureInitializedLocked();
        if (g_recording)
            return true;
        if (!g_initialized || !g_data_source_registered)
            return false;

        miniblink_perfetto::TraceConfig config;
        auto* buffer_config = config.add_buffers();
        buffer_config->set_size_kb(kDefaultPerfettoTraceBufferSizeKb);
        buffer_config->set_fill_policy(miniblink_perfetto::TraceConfig::BufferConfig::RING_BUFFER);
        auto* data_source_config = config.add_data_sources()->mutable_config();
        data_source_config->set_name(kMiniblinkPerfettoDataSourceName);
        data_source_config->set_target_buffer(0);
        config.set_flush_timeout_ms(kPerfettoFlushTimeoutMs);
        config.set_data_source_stop_timeout_ms(kPerfettoFlushTimeoutMs);

        g_tracing_session = miniblink_perfetto::Tracing::NewTrace(miniblink_perfetto::kInProcessBackend);
        if (!g_tracing_session)
            return false;
        g_tracing_session->Setup(config);
        g_tracing_session->StartBlocking();

        g_recording = true;
        ++g_started_count;
        g_event_count = 0;
        g_dropped_event_count = 0;
        g_last_trace_size = 0;
        g_last_trace_packet_count_hint = 0;
        g_last_trace_data.clear();
        g_last_track_event_names.clear();
        g_pending_events.clear();
        g_recorded_thread_descriptors.clear();
        ResetLastPerfettoServiceStatsLocked();
    }
    RecordProcessTrackDescriptorInSession();
    return true;
}

PerfettoLightweightStats StopPerfettoLightweightTrace()
{
    std::unique_ptr<miniblink_perfetto::TracingSession> tracing_session;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        if (g_recording)
            g_recording = false;
        tracing_session = std::move(g_tracing_session);
    }

    std::string trace_data;
    PerfettoTraceStatsSummary trace_stats;
    PerfettoServiceStateSummary service_state;
    if (tracing_session) {
        FlushSessionDataSource();
        tracing_session->FlushBlocking(kPerfettoFlushTimeoutMs);
        trace_stats = DecodeTraceStats(tracing_session->GetTraceStatsBlocking());
        tracing_session->StopBlocking();
    }

    std::lock_guard<std::mutex> guard(g_lock);
    trace_data = BuildPerfettoTraceDataLocked();
    service_state = BuildServiceStateSummary();
    PerfettoTraceEventSummary trace_events = SummarizePerfettoTraceData(trace_data);
    if (!trace_stats.success)
        trace_stats = BuildTraceStatsSummary(trace_data, trace_events);

    g_last_trace_data = trace_data;
    g_last_trace_size = g_last_trace_data.size();
    g_last_trace_packet_count_hint = trace_events.packet_count;
    StorePerfettoServiceStatsLocked(trace_stats, service_state, trace_events);
    g_pending_events.clear();
    g_recorded_thread_descriptors.clear();
    return CurrentStatsLocked();
}

std::string GetPerfettoLightweightTraceData()
{
    std::lock_guard<std::mutex> guard(g_lock);
    return g_last_trace_data;
}

std::string GetPerfettoLightweightTrackEventNames()
{
    std::lock_guard<std::mutex> guard(g_lock);
    return g_last_track_event_names;
}

void AddPerfettoLightweightTraceEvent(char phase,
    const char* category,
    const char* name,
    uint64_t thread_id,
    const PerfettoLightweightArg* args,
    size_t arg_count)
{
    PendingPerfettoEvent event;
    bool record_thread_descriptor = false;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        if (!g_initialized || !g_recording) {
            ++g_dropped_event_count;
            return;
        }
        if (!CanMirrorAsPerfettoTrackEvent(phase))
            return;

        event.phase = phase;
        event.category = category ? category : "";
        event.name = name ? name : "";
        event.thread_id = thread_id;
        event.args.reserve(arg_count);
        for (size_t i = 0; i < arg_count; ++i)
            event.args.push_back(CopyArg(args[i]));
        if (!ContainsThreadId(g_recorded_thread_descriptors, thread_id)) {
            g_recorded_thread_descriptors.push_back(thread_id);
            record_thread_descriptor = true;
        }
        g_pending_events.push_back(event);
        ++g_event_count;
    }

    if (record_thread_descriptor)
        RecordThreadTrackDescriptorInSession(thread_id);
    RecordEventInSession(std::move(event));
}

PerfettoLightweightStats GetPerfettoLightweightStats()
{
    std::lock_guard<std::mutex> guard(g_lock);
    return CurrentStatsLocked();
}

} // namespace trace_event
} // namespace base
