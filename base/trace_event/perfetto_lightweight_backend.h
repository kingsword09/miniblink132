// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_PERFETTO_LIGHTWEIGHT_BACKEND_H_
#define BASE_TRACE_EVENT_PERFETTO_LIGHTWEIGHT_BACKEND_H_

#include <stddef.h>
#include <stdint.h>

#include <string>

namespace base {
namespace trace_event {

enum class PerfettoLightweightArgType {
    kBool,
    kUInt,
    kInt,
    kDouble,
    kPointer,
    kString,
};

struct PerfettoLightweightArg {
    const char* name = nullptr;
    PerfettoLightweightArgType type = PerfettoLightweightArgType::kString;
    bool bool_value = false;
    uint64_t uint_value = 0;
    int64_t int_value = 0;
    double double_value = 0;
    const void* pointer_value = nullptr;
    const char* string_value = nullptr;
};

struct PerfettoLightweightStats {
    bool initialized = false;
    bool recording = false;
    uint64_t started_count = 0;
    uint64_t event_count = 0;
    uint64_t dropped_event_count = 0;
    size_t last_trace_size = 0;
    size_t last_trace_packet_count_hint = 0;
    bool last_trace_stats_success = false;
    size_t last_trace_stats_size = 0;
    uint32_t trace_stats_producers_connected = 0;
    uint64_t trace_stats_producers_seen = 0;
    uint32_t trace_stats_data_sources_registered = 0;
    uint64_t trace_stats_data_sources_seen = 0;
    uint32_t trace_stats_tracing_sessions = 0;
    uint32_t trace_stats_total_buffers = 0;
    uint64_t trace_stats_bytes_written = 0;
    uint64_t trace_stats_chunks_written = 0;
    uint64_t trace_packet_count = 0;
    uint64_t track_descriptor_packet_count = 0;
    uint64_t process_descriptor_count = 0;
    uint64_t thread_descriptor_count = 0;
    uint64_t track_event_packet_count = 0;
    uint64_t track_event_slice_begin_count = 0;
    uint64_t track_event_slice_end_count = 0;
    uint64_t track_event_instant_count = 0;
    uint64_t track_event_counter_count = 0;
    uint64_t track_event_counter_value_count = 0;
    uint64_t track_event_named_count = 0;
    uint64_t track_event_direct_name_count = 0;
    uint64_t track_event_interned_name_count = 0;
    uint64_t track_event_track_name_count = 0;
    bool last_service_state_success = false;
    size_t last_service_state_size = 0;
    int service_state_producer_count = 0;
    int service_state_data_source_count = 0;
    int service_state_tracing_session_count = 0;
    bool service_state_supports_tracing_sessions = false;
    int service_state_num_sessions = 0;
    int service_state_num_sessions_started = 0;
};

void InitializePerfettoLightweightBackend();
bool StartPerfettoLightweightTrace(const char* category_filter, size_t buffer_size_kb);
PerfettoLightweightStats StopPerfettoLightweightTrace();
std::string GetPerfettoLightweightTraceData();
std::string GetPerfettoLightweightTrackEventNames();
void AddPerfettoLightweightTraceEvent(char phase,
    const char* category,
    const char* name,
    uint64_t thread_id,
    const PerfettoLightweightArg* args,
    size_t arg_count);
PerfettoLightweightStats GetPerfettoLightweightStats();

} // namespace trace_event
} // namespace base

#endif // BASE_TRACE_EVENT_PERFETTO_LIGHTWEIGHT_BACKEND_H_
