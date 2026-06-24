// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/process/process_handle.h"
#include "base/memory/ref_counted_memory.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/named_trigger.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/trace_event/trace_config.h"
#include "base/trace_event/trace_event_impl.h"
#include "base/trace_event/trace_event_memory_overhead.h"
#include "base/trace_event/trace_log.h"
#include "base/trace_event/trace_event_stub.h"
#include "base/unguessable_token.h"

#if BUILDFLAG(USE_PERFETTO_TRACE_PROCESSOR)
namespace perfetto {
namespace trace_processor {
class TraceProcessorStorage {
};
} // namespace trace_processor
} // namespace perfetto
#endif

namespace base {
namespace trace_event {

namespace {

MemoryAllocatorDumpGuid MakeDumpGuid(std::string_view absolute_name)
{
    return MemoryAllocatorDumpGuid(std::string(absolute_name));
}

const unsigned char* DisabledCategoryState()
{
    static const unsigned char disabled = 0;
    return &disabled;
}

} // namespace

ConvertableToTraceFormat::~ConvertableToTraceFormat() = default;

void TracedValue::AppendAsTraceFormat(std::string* out) const
{
}

MemoryDumpProvider::~MemoryDumpProvider() = default;

// static
constexpr const char* const MemoryDumpManager::kTraceCategory;

// static
MemoryDumpManager* MemoryDumpManager::GetInstance()
{
    static MemoryDumpManager instance;
    return &instance;
}

void MemoryDumpManager::RegisterDumpProvider(MemoryDumpProvider*,
    const char*,
    scoped_refptr<SingleThreadTaskRunner>)
{
}

void MemoryDumpManager::RegisterDumpProvider(MemoryDumpProvider*,
    const char*,
    scoped_refptr<SingleThreadTaskRunner>,
    MemoryDumpProvider::Options)
{
}

void MemoryDumpManager::RegisterDumpProviderWithSequencedTaskRunner(MemoryDumpProvider*,
    const char*,
    scoped_refptr<SequencedTaskRunner>,
    MemoryDumpProvider::Options)
{
}

void MemoryDumpManager::UnregisterDumpProvider(MemoryDumpProvider*)
{
}

TraceEvent::TraceEvent() = default;

TraceEvent::TraceEvent(PlatformThreadId thread_id,
    TimeTicks timestamp,
    ThreadTicks thread_timestamp,
    char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    const char* scope,
    unsigned long long id,
    unsigned long long bind_id,
    TraceArguments* args,
    unsigned int flags)
{
    Reset(thread_id, timestamp, thread_timestamp, phase, category_group_enabled,
        name, scope, id, bind_id, args, flags);
}

TraceEvent::~TraceEvent() = default;
TraceEvent::TraceEvent(TraceEvent&& other) noexcept = default;
TraceEvent& TraceEvent::operator=(TraceEvent&& other) noexcept = default;

void TraceEvent::Reset()
{
    timestamp_ = TimeTicks();
    thread_timestamp_ = ThreadTicks();
    duration_ = TimeDelta::FromInternalValue(-1);
    thread_duration_ = TimeDelta();
    scope_ = nullptr;
    id_ = 0;
    category_group_enabled_ = nullptr;
    name_ = nullptr;
    args_.Reset();
    thread_id_ = 0;
    flags_ = 0;
    bind_id_ = 0;
    phase_ = TRACE_EVENT_PHASE_BEGIN;
}

void TraceEvent::Reset(PlatformThreadId thread_id,
    TimeTicks timestamp,
    ThreadTicks thread_timestamp,
    char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    const char* scope,
    unsigned long long id,
    unsigned long long bind_id,
    TraceArguments* args,
    unsigned int flags)
{
    args_.Reset();
    thread_id_ = thread_id;
    timestamp_ = timestamp;
    thread_timestamp_ = thread_timestamp;
    duration_ = TimeDelta::FromInternalValue(-1);
    thread_duration_ = TimeDelta();
    phase_ = phase;
    category_group_enabled_ = category_group_enabled;
    name_ = name;
    scope_ = scope;
    id_ = id;
    bind_id_ = bind_id;
    flags_ = flags;
    if (args)
        args_ = std::move(*args);
}

void TraceEvent::UpdateDuration(const TimeTicks& now, const ThreadTicks& thread_now)
{
    duration_ = now - timestamp_;
    thread_duration_ = thread_now.is_null() ? ThreadTicks() - ThreadTicks() : thread_now - thread_timestamp_;
}

void TraceEvent::EstimateTraceMemoryOverhead(TraceEventMemoryOverhead* overhead)
{
    if (overhead)
        overhead->Add(TraceEventMemoryOverhead::kTraceEvent, sizeof(*this));
}

void TraceEvent::AppendAsJSON(std::string* out, const ArgumentFilterPredicate&) const
{
    if (out)
        out->append("{}");
}

void TraceEvent::AppendPrettyPrinted(std::ostringstream* out) const
{
    if (out)
        *out << name();
}

TraceArguments& TraceArguments::operator=(TraceArguments&& other) noexcept
{
    if (this != &other) {
        this->~TraceArguments();
        new (this) TraceArguments(std::move(other));
    }
    return *this;
}

TraceArguments::TraceArguments(int num_args,
    const char* const* arg_names,
    const unsigned char* arg_types,
    const unsigned long long* arg_values)
{
    if (num_args > static_cast<int>(kMaxSize))
        num_args = static_cast<int>(kMaxSize);
    size_ = static_cast<unsigned char>(std::max(num_args, 0));
    for (size_t i = 0; i < size_; ++i) {
        types_[i] = arg_types ? arg_types[i] : TRACE_VALUE_TYPE_UINT;
        names_[i] = arg_names ? arg_names[i] : nullptr;
    }
}

void TraceArguments::Reset()
{
    size_ = 0;
}

void TraceArguments::AppendDebugString(std::string* out)
{
    if (out)
        out->append("{}");
}

TraceConfig::MemoryDumpConfig::HeapProfiler::HeapProfiler()
    : breakdown_threshold_bytes(kDefaultBreakdownThresholdBytes)
{
}

void TraceConfig::MemoryDumpConfig::HeapProfiler::Clear()
{
    breakdown_threshold_bytes = kDefaultBreakdownThresholdBytes;
}

TraceConfig::MemoryDumpConfig::MemoryDumpConfig() = default;
TraceConfig::MemoryDumpConfig::MemoryDumpConfig(const MemoryDumpConfig& other) = default;
TraceConfig::MemoryDumpConfig::~MemoryDumpConfig() = default;

void TraceConfig::MemoryDumpConfig::Clear()
{
    allowed_dump_modes.clear();
    triggers.clear();
    heap_profiler_options.Clear();
}

void TraceConfig::MemoryDumpConfig::Merge(const TraceConfig::MemoryDumpConfig& config)
{
    allowed_dump_modes.insert(config.allowed_dump_modes.begin(), config.allowed_dump_modes.end());
    triggers.insert(triggers.end(), config.triggers.begin(), config.triggers.end());
    heap_profiler_options.breakdown_threshold_bytes = std::min(
        heap_profiler_options.breakdown_threshold_bytes,
        config.heap_profiler_options.breakdown_threshold_bytes);
}

TraceConfig::ProcessFilterConfig::ProcessFilterConfig() = default;

TraceConfig::ProcessFilterConfig::ProcessFilterConfig(
    const std::unordered_set<base::ProcessId>& included_process_ids)
    : included_process_ids_(included_process_ids)
{
}

TraceConfig::ProcessFilterConfig::ProcessFilterConfig(const ProcessFilterConfig&) = default;
TraceConfig::ProcessFilterConfig::~ProcessFilterConfig() = default;

void TraceConfig::ProcessFilterConfig::Clear()
{
    included_process_ids_.clear();
}

void TraceConfig::ProcessFilterConfig::Merge(const ProcessFilterConfig& config)
{
    included_process_ids_.insert(config.included_process_ids_.begin(),
        config.included_process_ids_.end());
}

void TraceConfig::ProcessFilterConfig::InitializeFromConfigDict(const Value::Dict&)
{
    Clear();
}

void TraceConfig::ProcessFilterConfig::ToDict(Value::Dict&) const
{
}

bool TraceConfig::ProcessFilterConfig::IsEnabled(base::ProcessId process_id) const
{
    return included_process_ids_.empty() || included_process_ids_.count(process_id) != 0;
}

TraceConfig::EventFilterConfig::EventFilterConfig(const std::string& predicate_name)
    : predicate_name_(predicate_name)
{
}

TraceConfig::EventFilterConfig::EventFilterConfig(const EventFilterConfig& other)
    : predicate_name_(other.predicate_name_)
    , category_filter_(other.category_filter_)
    , args_(other.args_.Clone())
{
}

TraceConfig::EventFilterConfig::~EventFilterConfig() = default;

TraceConfig::EventFilterConfig& TraceConfig::EventFilterConfig::operator=(const EventFilterConfig& rhs)
{
    if (this == &rhs)
        return *this;
    predicate_name_ = rhs.predicate_name_;
    category_filter_ = rhs.category_filter_;
    args_ = rhs.args_.Clone();
    return *this;
}

bool TraceConfig::EventFilterConfig::IsEquivalentTo(const EventFilterConfig& other) const
{
    return predicate_name_ == other.predicate_name_
        && category_filter_.IsEquivalentTo(other.category_filter_)
        && args_ == other.args_;
}

void TraceConfig::EventFilterConfig::InitializeFromConfigDict(const Value::Dict&)
{
}

void TraceConfig::EventFilterConfig::SetCategoryFilter(const TraceConfigCategoryFilter& category_filter)
{
    category_filter_ = category_filter;
}

void TraceConfig::EventFilterConfig::ToDict(Value::Dict&) const
{
}

bool TraceConfig::EventFilterConfig::GetArgAsSet(const char*, std::unordered_set<std::string>* out_set) const
{
    if (out_set)
        out_set->clear();
    return false;
}

bool TraceConfig::EventFilterConfig::IsCategoryGroupEnabled(std::string_view category_group_name) const
{
    return category_filter_.IsCategoryGroupEnabled(category_group_name);
}

TraceConfig::TraceConfig()
{
    Clear();
}

TraceConfig::TraceConfig(std::string_view, std::string_view)
{
    Clear();
}

TraceConfig::TraceConfig(std::string_view, TraceRecordMode record_mode)
{
    Clear();
    record_mode_ = record_mode;
}

TraceConfig::TraceConfig(std::string_view)
{
    Clear();
}

TraceConfig::TraceConfig(const Value::Dict&)
{
    Clear();
}

TraceConfig::TraceConfig(const TraceConfig& other)
{
    *this = other;
}

TraceConfig::~TraceConfig() = default;

TraceConfig& TraceConfig::operator=(const TraceConfig& rhs)
{
    if (this == &rhs)
        return *this;
    record_mode_ = rhs.record_mode_;
    trace_buffer_size_in_events_ = rhs.trace_buffer_size_in_events_;
    trace_buffer_size_in_kb_ = rhs.trace_buffer_size_in_kb_;
    enable_systrace_ = rhs.enable_systrace_;
    enable_argument_filter_ = rhs.enable_argument_filter_;
    category_filter_ = rhs.category_filter_;
    memory_dump_config_ = rhs.memory_dump_config_;
    process_filter_config_ = rhs.process_filter_config_;
    event_filters_ = rhs.event_filters_;
    enable_event_package_name_filter_ = rhs.enable_event_package_name_filter_;
    histogram_names_ = rhs.histogram_names_;
    systrace_events_ = rhs.systrace_events_;
    return *this;
}

bool TraceConfig::IsEquivalentTo(const TraceConfig& other) const
{
    return record_mode_ == other.record_mode_
        && trace_buffer_size_in_events_ == other.trace_buffer_size_in_events_
        && trace_buffer_size_in_kb_ == other.trace_buffer_size_in_kb_
        && enable_systrace_ == other.enable_systrace_
        && enable_argument_filter_ == other.enable_argument_filter_
        && enable_event_package_name_filter_ == other.enable_event_package_name_filter_
        && category_filter_.IsEquivalentTo(other.category_filter_)
        && memory_dump_config_ == other.memory_dump_config_
        && process_filter_config_ == other.process_filter_config_
        && event_filters_.size() == other.event_filters_.size()
        && std::equal(event_filters_.begin(), event_filters_.end(), other.event_filters_.begin(),
            [](const EventFilterConfig& lhs, const EventFilterConfig& rhs) {
                return lhs.IsEquivalentTo(rhs);
            })
        && histogram_names_ == other.histogram_names_
        && systrace_events_ == other.systrace_events_;
}

void TraceConfig::EnableSystraceEvent(const std::string& systrace_event)
{
    enable_systrace_ = true;
    systrace_events_.insert(systrace_event);
}

void TraceConfig::EnableHistogram(const std::string& histogram_name)
{
    histogram_names_.insert(histogram_name);
}

std::string TraceConfig::ToString() const
{
    return "{}";
}

std::unique_ptr<ConvertableToTraceFormat> TraceConfig::AsConvertableToTraceFormat() const
{
    return nullptr;
}

std::string TraceConfig::ToCategoryFilterString() const
{
    return {};
}

std::string TraceConfig::ToTraceOptionsString() const
{
    return {};
}

std::string TraceConfig::ToPerfettoTrackEventConfigRaw(bool) const
{
    return {};
}

bool TraceConfig::IsCategoryGroupEnabled(std::string_view) const
{
    return false;
}

void TraceConfig::Merge(const TraceConfig& config)
{
    *this = config;
}

void TraceConfig::Clear()
{
    record_mode_ = RECORD_UNTIL_FULL;
    trace_buffer_size_in_events_ = 0;
    trace_buffer_size_in_kb_ = 0;
    enable_systrace_ = false;
    enable_argument_filter_ = false;
    category_filter_.Clear();
    memory_dump_config_.Clear();
    process_filter_config_.Clear();
    event_filters_.clear();
    enable_event_package_name_filter_ = false;
    histogram_names_.clear();
    systrace_events_.clear();
}

void TraceConfig::ResetMemoryDumpConfig(const MemoryDumpConfig& memory_dump_config)
{
    memory_dump_config_ = memory_dump_config;
}

void TraceConfig::SetProcessFilterConfig(const ProcessFilterConfig& config)
{
    process_filter_config_ = config;
}

MemoryAllocatorDump::Entry::Entry()
    : entry_type(kString)
    , value_uint64(0)
{
}

MemoryAllocatorDump::Entry::Entry(std::string entry_name, std::string entry_units, uint64_t value)
    : name(std::move(entry_name))
    , units(std::move(entry_units))
    , entry_type(kUint64)
    , value_uint64(value)
{
}

MemoryAllocatorDump::Entry::Entry(std::string entry_name, std::string entry_units, std::string value)
    : name(std::move(entry_name))
    , units(std::move(entry_units))
    , entry_type(kString)
    , value_uint64(0)
    , value_string(std::move(value))
{
}

MemoryAllocatorDump::Entry::Entry(Entry&& other) noexcept = default;
MemoryAllocatorDump::Entry& MemoryAllocatorDump::Entry::operator=(Entry&& other) = default;

bool MemoryAllocatorDump::Entry::operator==(const Entry& rhs) const
{
    return name == rhs.name
        && units == rhs.units
        && entry_type == rhs.entry_type
        && value_uint64 == rhs.value_uint64
        && value_string == rhs.value_string;
}

MemoryAllocatorDump::MemoryAllocatorDump(const std::string& absolute_name,
    MemoryDumpLevelOfDetail level_of_detail,
    const MemoryAllocatorDumpGuid& guid)
    : absolute_name_(absolute_name)
    , guid_(guid)
    , level_of_detail_(level_of_detail)
    , flags_(Flags::kDefault)
{
}

MemoryAllocatorDump::~MemoryAllocatorDump() = default;

const char MemoryAllocatorDump::kNameSize[] = "size";
const char MemoryAllocatorDump::kNameObjectCount[] = "object_count";
const char MemoryAllocatorDump::kUnitsBytes[] = "bytes";
const char MemoryAllocatorDump::kUnitsObjects[] = "objects";
const char MemoryAllocatorDump::kTypeScalar[] = "scalar";
const char MemoryAllocatorDump::kTypeString[] = "string";

void MemoryAllocatorDump::AddScalar(const char* name, const char* units, uint64_t value)
{
    cached_size_.reset();
    entries_.emplace_back(name ? name : "", units ? units : "", value);
}

void MemoryAllocatorDump::AddString(const char* name, const char* units, const std::string& value)
{
    cached_size_.reset();
    entries_.emplace_back(name ? name : "", units ? units : "", value);
}

void MemoryAllocatorDump::AsValueInto(TracedValue*) const
{
}

void MemoryAllocatorDump::AsProtoInto(
    perfetto::protos::pbzero::MemoryTrackerSnapshot_ProcessSnapshot_MemoryNode*) const
{
}

uint64_t MemoryAllocatorDump::GetSizeInternal() const
{
    if (cached_size_)
        return *cached_size_;
    for (const Entry& entry : entries_) {
        if (entry.entry_type == Entry::kUint64 && entry.name == kNameSize && entry.units == kUnitsBytes) {
            cached_size_ = entry.value_uint64;
            return entry.value_uint64;
        }
    }
    cached_size_ = 0;
    return 0;
}

void PrintTo(const MemoryAllocatorDump::Entry& entry, std::ostream* out)
{
    if (!out)
        return;
    *out << "<Entry(\"" << entry.name << "\", \"" << entry.units << "\", ";
    if (entry.entry_type == MemoryAllocatorDump::Entry::kUint64)
        *out << entry.value_uint64;
    else
        *out << "\"" << entry.value_string << "\"";
    *out << ")>";
}

bool ProcessMemoryDump::is_black_hole_non_fatal_for_testing_ = true;

#if defined(COUNT_RESIDENT_BYTES_SUPPORTED)
size_t ProcessMemoryDump::GetSystemPageSize()
{
    return 4096;
}

std::optional<size_t> ProcessMemoryDump::CountResidentBytes(void*, size_t)
{
    return std::nullopt;
}

std::optional<size_t> ProcessMemoryDump::CountResidentBytesInSharedMemory(void*, size_t)
{
    return std::nullopt;
}
#endif

ProcessMemoryDump::ProcessMemoryDump(const MemoryDumpArgs& dump_args)
    : dump_args_(dump_args)
{
}

ProcessMemoryDump::ProcessMemoryDump(ProcessMemoryDump&& other) = default;
ProcessMemoryDump& ProcessMemoryDump::operator=(ProcessMemoryDump&& other) = default;
ProcessMemoryDump::~ProcessMemoryDump() = default;

MemoryAllocatorDump* ProcessMemoryDump::CreateAllocatorDump(const std::string& absolute_name)
{
    return CreateAllocatorDump(absolute_name, GetDumpId(absolute_name));
}

MemoryAllocatorDump* ProcessMemoryDump::CreateAllocatorDump(const std::string& absolute_name,
    const MemoryAllocatorDumpGuid& guid)
{
    return AddAllocatorDumpInternal(
        std::make_unique<MemoryAllocatorDump>(absolute_name, dump_args_.level_of_detail, guid));
}

MemoryAllocatorDump* ProcessMemoryDump::GetAllocatorDump(const std::string& absolute_name) const
{
    auto it = allocator_dumps_.find(absolute_name);
    return it == allocator_dumps_.end() ? nullptr : it->second.get();
}

MemoryAllocatorDump* ProcessMemoryDump::GetOrCreateAllocatorDump(const std::string& absolute_name)
{
    if (MemoryAllocatorDump* dump = GetAllocatorDump(absolute_name))
        return dump;
    return CreateAllocatorDump(absolute_name);
}

MemoryAllocatorDump* ProcessMemoryDump::CreateSharedGlobalAllocatorDump(const MemoryAllocatorDumpGuid& guid)
{
    const std::string name = "global/" + guid.ToString();
    return GetOrCreateAllocatorDump(name);
}

MemoryAllocatorDump* ProcessMemoryDump::CreateWeakSharedGlobalAllocatorDump(const MemoryAllocatorDumpGuid& guid)
{
    MemoryAllocatorDump* dump = CreateSharedGlobalAllocatorDump(guid);
    if (dump)
        dump->set_flags(MemoryAllocatorDump::kWeak);
    return dump;
}

MemoryAllocatorDump* ProcessMemoryDump::GetSharedGlobalAllocatorDump(const MemoryAllocatorDumpGuid& guid) const
{
    return GetAllocatorDump("global/" + guid.ToString());
}

void ProcessMemoryDump::SetAllocatorDumpsForSerialization(std::vector<std::unique_ptr<MemoryAllocatorDump>> dumps)
{
    allocator_dumps_.clear();
    for (auto& dump : dumps)
        AddAllocatorDumpInternal(std::move(dump));
}

std::vector<ProcessMemoryDump::MemoryAllocatorDumpEdge> ProcessMemoryDump::GetAllEdgesForSerialization() const
{
    std::vector<MemoryAllocatorDumpEdge> edges;
    edges.reserve(allocator_dumps_edges_.size());
    for (const auto& it : allocator_dumps_edges_)
        edges.push_back(it.second);
    return edges;
}

void ProcessMemoryDump::SetAllEdgesForSerialization(const std::vector<MemoryAllocatorDumpEdge>& edges)
{
    allocator_dumps_edges_.clear();
    for (const MemoryAllocatorDumpEdge& edge : edges)
        allocator_dumps_edges_[edge.source] = edge;
}

void ProcessMemoryDump::DumpHeapUsage(
    const std::unordered_map<base::trace_event::AllocationContext, base::trace_event::AllocationMetrics>&,
    base::trace_event::TraceEventMemoryOverhead&,
    const char*)
{
}

void ProcessMemoryDump::AddOwnershipEdge(const MemoryAllocatorDumpGuid& source,
    const MemoryAllocatorDumpGuid& target,
    int importance)
{
    allocator_dumps_edges_[source] = { source, target, importance, false };
}

void ProcessMemoryDump::AddOwnershipEdge(const MemoryAllocatorDumpGuid& source,
    const MemoryAllocatorDumpGuid& target)
{
    AddOwnershipEdge(source, target, 0);
}

void ProcessMemoryDump::AddOverridableOwnershipEdge(const MemoryAllocatorDumpGuid& source,
    const MemoryAllocatorDumpGuid& target,
    int importance)
{
    allocator_dumps_edges_[source] = { source, target, importance, true };
}

void ProcessMemoryDump::CreateSharedMemoryOwnershipEdge(const MemoryAllocatorDumpGuid& client_local_dump_guid,
    const UnguessableToken& shared_memory_guid,
    int importance)
{
    AddOwnershipEdge(client_local_dump_guid, MemoryAllocatorDumpGuid(shared_memory_guid.ToString()), importance);
}

void ProcessMemoryDump::CreateWeakSharedMemoryOwnershipEdge(const MemoryAllocatorDumpGuid& client_local_dump_guid,
    const UnguessableToken& shared_memory_guid,
    int importance)
{
    CreateSharedMemoryOwnershipEdge(client_local_dump_guid, shared_memory_guid, importance);
}

void ProcessMemoryDump::AddSuballocation(const MemoryAllocatorDumpGuid& source,
    const std::string& target_node_name)
{
    MemoryAllocatorDump* child = CreateAllocatorDump(target_node_name + "/__" + source.ToString());
    if (child)
        AddOwnershipEdge(source, child->guid());
}

void ProcessMemoryDump::Clear()
{
    allocator_dumps_.clear();
    allocator_dumps_edges_.clear();
}

void ProcessMemoryDump::TakeAllDumpsFrom(ProcessMemoryDump* other)
{
    if (!other)
        return;
    for (auto& it : other->allocator_dumps_)
        allocator_dumps_[it.first] = std::move(it.second);
    other->allocator_dumps_.clear();
    allocator_dumps_edges_.insert(other->allocator_dumps_edges_.begin(), other->allocator_dumps_edges_.end());
    other->allocator_dumps_edges_.clear();
}

void ProcessMemoryDump::SerializeAllocatorDumpsInto(TracedValue*) const
{
}

void ProcessMemoryDump::SerializeAllocatorDumpsInto(
    perfetto::protos::pbzero::MemoryTrackerSnapshot*,
    const base::ProcessId) const
{
}

MemoryAllocatorDump* ProcessMemoryDump::AddAllocatorDumpInternal(std::unique_ptr<MemoryAllocatorDump> dump)
{
    if (!dump)
        return nullptr;
    const std::string name = dump->absolute_name();
    auto result = allocator_dumps_.emplace(name, std::move(dump));
    return result.first->second.get();
}

MemoryAllocatorDumpGuid ProcessMemoryDump::GetDumpId(const std::string& absolute_name)
{
    return MakeDumpGuid(absolute_name);
}

void ProcessMemoryDump::CreateSharedMemoryOwnershipEdgeInternal(
    const MemoryAllocatorDumpGuid& client_local_dump_guid,
    const UnguessableToken& shared_memory_guid,
    int importance,
    bool)
{
    CreateSharedMemoryOwnershipEdge(client_local_dump_guid, shared_memory_guid, importance);
}

MemoryAllocatorDump* ProcessMemoryDump::GetBlackHoleMad(const std::string& absolute_name)
{
    if (!black_hole_mad_)
        black_hole_mad_ = std::make_unique<MemoryAllocatorDump>(
            absolute_name.empty() ? "discarded" : absolute_name,
            dump_args_.level_of_detail,
            GetDumpId(absolute_name.empty() ? "discarded" : absolute_name));
    return black_hole_mad_.get();
}

class TraceBufferChunk {
};

class TraceBuffer {
};

class JsonStringOutputWriter {
};

struct TraceLog::RegisteredAsyncObserver {
};

TraceLog::TraceLog(int generation)
    : process_sort_index_(0)
    , process_id_hash_(0)
    , process_id_(base::kNullProcessId)
    , trace_options_(kInternalNone)
    , thread_shared_chunk_index_(0)
    , generation_(generation)
    , use_worker_thread_(false)
{
}

TraceLog::~TraceLog() = default;

bool TraceLog::OnMemoryDump(const MemoryDumpArgs&, ProcessMemoryDump*)
{
    return false;
}

TraceLog* TraceLog::GetInstance()
{
    static TraceLog* instance = new TraceLog(0);
    return instance;
}

TraceConfig TraceLog::GetCurrentTraceConfig() const
{
    return trace_config_;
}

void TraceLog::InitializeThreadLocalEventBufferIfSupported()
{
}

void TraceLog::SetEnabled(const TraceConfig& trace_config, uint8_t)
{
    enabled_ = true;
    ++num_traces_recorded_;
    trace_config_ = trace_config;
    if (track_event_sessions_.empty())
        track_event_sessions_.push_back({ 0 });
}

void TraceLog::SetDisabled()
{
    SetDisabled(RECORDING_MODE);
}

void TraceLog::SetDisabled(uint8_t)
{
    enabled_ = false;
    track_event_sessions_.clear();
}

int TraceLog::GetNumTracesRecorded()
{
    return enabled_ ? num_traces_recorded_ : -1;
}

void TraceLog::AddEnabledStateObserver(EnabledStateObserver* listener)
{
    if (!listener || HasEnabledStateObserver(listener))
        return;
    enabled_state_observers_.push_back(listener);
}

void TraceLog::RemoveEnabledStateObserver(EnabledStateObserver* listener)
{
    enabled_state_observers_.erase(
        std::remove(enabled_state_observers_.begin(), enabled_state_observers_.end(), listener),
        enabled_state_observers_.end());
}

void TraceLog::AddOwnedEnabledStateObserver(std::unique_ptr<EnabledStateObserver> listener)
{
    if (!listener)
        return;
    AddEnabledStateObserver(listener.get());
    owned_enabled_state_observer_copy_.push_back(std::move(listener));
}

bool TraceLog::HasEnabledStateObserver(EnabledStateObserver* listener) const
{
    return std::find(enabled_state_observers_.begin(), enabled_state_observers_.end(), listener)
        != enabled_state_observers_.end();
}

void TraceLog::AddAsyncEnabledStateObserver(WeakPtr<AsyncEnabledStateObserver>)
{
}

void TraceLog::RemoveAsyncEnabledStateObserver(AsyncEnabledStateObserver*)
{
}

bool TraceLog::HasAsyncEnabledStateObserver(AsyncEnabledStateObserver*) const
{
    return false;
}

void TraceLog::AddIncrementalStateObserver(IncrementalStateObserver* listener)
{
    if (!listener)
        return;
    if (std::find(incremental_state_observers_.begin(), incremental_state_observers_.end(), listener)
        == incremental_state_observers_.end())
        incremental_state_observers_.push_back(listener);
}

void TraceLog::RemoveIncrementalStateObserver(IncrementalStateObserver* listener)
{
    incremental_state_observers_.erase(
        std::remove(incremental_state_observers_.begin(), incremental_state_observers_.end(), listener),
        incremental_state_observers_.end());
}

TraceLogStatus TraceLog::GetStatus() const
{
    TraceLogStatus status;
    status.event_capacity = 0;
    status.event_count = 0;
    return status;
}

void TraceLog::EstimateTraceMemoryOverhead(TraceEventMemoryOverhead* overhead)
{
    if (overhead)
        overhead->Add(TraceEventMemoryOverhead::kOther, sizeof(*this));
}

void TraceLog::SetArgumentFilterPredicate(const ArgumentFilterPredicate& argument_filter_predicate)
{
    argument_filter_predicate_ = argument_filter_predicate;
}

ArgumentFilterPredicate TraceLog::GetArgumentFilterPredicate() const
{
    return argument_filter_predicate_;
}

void TraceLog::SetMetadataFilterPredicate(const MetadataFilterPredicate& metadata_filter_predicate)
{
    metadata_filter_predicate_ = metadata_filter_predicate;
}

MetadataFilterPredicate TraceLog::GetMetadataFilterPredicate() const
{
    return metadata_filter_predicate_;
}

void TraceLog::SetRecordHostAppPackageName(bool record_host_app_package_name)
{
    record_host_app_package_name_ = record_host_app_package_name;
}

bool TraceLog::ShouldRecordHostAppPackageName() const
{
    return record_host_app_package_name_;
}

void TraceLog::Flush(const OutputCallback& cb, bool)
{
    if (cb)
        cb.Run(nullptr, false);
}

void TraceLog::CancelTracing(const OutputCallback& cb)
{
    SetDisabled();
    if (cb)
        cb.Run(nullptr, false);
}

void TraceLog::SetAddTraceEventOverrides(const AddTraceEventOverrideFunction& add_event_override,
    const OnFlushFunction& on_flush_callback,
    const UpdateDurationFunction& update_duration_callback)
{
    add_trace_event_override_ = add_event_override;
    on_flush_override_ = on_flush_callback;
    update_duration_override_ = update_duration_callback;
}

const unsigned char* TraceLog::GetCategoryGroupEnabled(const char* name)
{
    TraceCategory* category = CategoryRegistry::GetCategoryByName(name);
    return category ? category->state_ptr() : DisabledCategoryState();
}

const char* TraceLog::GetCategoryGroupName(const unsigned char* category_group_enabled)
{
    const TraceCategory* category = CategoryRegistry::GetCategoryByStatePtr(category_group_enabled);
    return category && category->is_valid() ? category->name() : "";
}

bool TraceLog::ShouldAddAfterUpdatingState(char,
    const unsigned char*,
    const char*,
    uint64_t,
    PlatformThreadId,
    const TimeTicks,
    TraceArguments*)
{
    return false;
}

TraceEventHandle TraceLog::AddTraceEvent(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    TraceArguments*,
    unsigned int)
{
    return {};
}

TraceEventHandle TraceLog::AddTraceEventWithBindId(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    uint64_t,
    TraceArguments*,
    unsigned int)
{
    return {};
}

TraceEventHandle TraceLog::AddTraceEventWithProcessId(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    ProcessId,
    TraceArguments*,
    unsigned int)
{
    return {};
}

TraceEventHandle TraceLog::AddTraceEventWithThreadIdAndTimestamp(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    PlatformThreadId,
    const TimeTicks&,
    TraceArguments*,
    unsigned int)
{
    return {};
}

TraceEventHandle TraceLog::AddTraceEventWithThreadIdAndTimestamp(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    uint64_t,
    PlatformThreadId,
    const TimeTicks&,
    TraceArguments*,
    unsigned int)
{
    return {};
}

TraceEventHandle TraceLog::AddTraceEventWithThreadIdAndTimestamps(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    uint64_t,
    PlatformThreadId,
    const TimeTicks&,
    const ThreadTicks&,
    TraceArguments*,
    unsigned int)
{
    return {};
}

void TraceLog::AddMetadataEvent(const unsigned char*, const char*, TraceArguments*, unsigned int)
{
}

void TraceLog::UpdateTraceEventDuration(const unsigned char*, const char*, TraceEventHandle)
{
}

void TraceLog::UpdateTraceEventDurationExplicit(const unsigned char*,
    const char*,
    TraceEventHandle,
    PlatformThreadId,
    bool,
    const TimeTicks&,
    const ThreadTicks&)
{
}

uint64_t TraceLog::MangleEventId(uint64_t id)
{
    return id;
}

void TraceLog::ResetForTesting()
{
}

TraceEvent* TraceLog::GetEventByHandle(TraceEventHandle)
{
    return nullptr;
}

void TraceLog::SetProcessID(ProcessId process_id)
{
    process_id_ = process_id;
}

void TraceLog::SetProcessSortIndex(int sort_index)
{
    process_sort_index_ = sort_index;
}

void TraceLog::OnSetProcessName(const std::string&)
{
}

int TraceLog::GetNewProcessLabelId()
{
    return ++next_process_label_id_;
}

void TraceLog::UpdateProcessLabel(int label_id, const std::string& current_label)
{
    process_labels_[label_id] = current_label;
}

void TraceLog::RemoveProcessLabel(int label_id)
{
    process_labels_.erase(label_id);
}

void TraceLog::SetThreadSortIndex(PlatformThreadId thread_id, int sort_index)
{
    thread_sort_indices_[thread_id] = sort_index;
}

size_t TraceLog::GetObserverCountForTest() const
{
    return enabled_state_observers_.size();
}

void TraceLog::SetCurrentThreadBlocksMessageLoop()
{
}

void TraceLog::SetTraceBufferForTesting(std::unique_ptr<TraceBuffer>)
{
}

std::vector<TraceLog::TrackEventSession> TraceLog::GetTrackEventSessions() const
{
    return track_event_sessions_;
}

void TraceLog::InitializePerfettoIfNeeded()
{
}

bool TraceLog::IsPerfettoInitializedByTraceLog() const
{
    return false;
}

void TraceLog::OnIncrementalStateCleared()
{
    for (const auto& observer : incremental_state_observers_) {
        if (observer)
            observer->OnIncrementalStateCleared();
    }
}

void TraceLog::UpdateCategoryRegistry()
{
}

void TraceLog::UpdateCategoryState(TraceCategory*)
{
}

TraceLog::InternalTraceOptions TraceLog::GetInternalOptionsFromTraceConfig(const TraceConfig&)
{
    return kInternalNone;
}

void TraceLog::AddMetadataEventsWhileLocked()
{
}

TraceBuffer* TraceLog::CreateTraceBuffer()
{
    return nullptr;
}

std::string TraceLog::EventToConsoleMessage(char, const TimeTicks&, TraceEvent*)
{
    return {};
}

TraceEvent* TraceLog::AddEventToThreadSharedChunkWhileLocked(TraceEventHandle*, bool)
{
    return nullptr;
}

void TraceLog::CheckIfBufferIsFullWhileLocked()
{
}

void TraceLog::SetDisabledWhileLocked(uint8_t modes)
{
    SetDisabled(modes);
}

TraceEvent* TraceLog::GetEventByHandleInternal(TraceEventHandle, OptionalAutoLock*)
{
    return nullptr;
}

void TraceLog::FlushInternal(const OutputCallback& cb, bool, bool)
{
    Flush(cb);
}

void TraceLog::OnTraceData(const char*, size_t, bool)
{
}

void TraceLog::FlushCurrentThread(int, bool)
{
}

void TraceLog::ConvertTraceEventsToTraceFormat(std::unique_ptr<TraceBuffer>,
    const TraceLog::OutputCallback& flush_output_callback,
    const ArgumentFilterPredicate&)
{
    if (flush_output_callback)
        flush_output_callback.Run(nullptr, false);
}

void TraceLog::FinishFlush(int, bool)
{
}

void TraceLog::OnFlushTimeout(int, bool)
{
}

void TraceLog::UseNextTraceBuffer()
{
}

TraceLogStatus::TraceLogStatus()
    : event_capacity(0)
    , event_count(0)
{
}

TraceLogStatus::~TraceLogStatus() = default;

bool EmitNamedTrigger(const std::string&, std::optional<int32_t>)
{
    return false;
}

void NamedTriggerManager::SetInstance(NamedTriggerManager*)
{
}

} // namespace trace_event
} // namespace base

namespace trace_event_internal {

namespace {

base::trace_event::TraceEventHandle EmptyTraceEventHandle()
{
    return {};
}

} // namespace

base::trace_event::TraceEventHandle AddTraceEvent(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    base::trace_event::TraceArguments*,
    unsigned int)
{
    return EmptyTraceEventHandle();
}

base::trace_event::TraceEventHandle AddTraceEventWithBindId(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    uint64_t,
    base::trace_event::TraceArguments*,
    unsigned int)
{
    return EmptyTraceEventHandle();
}

base::trace_event::TraceEventHandle AddTraceEventWithProcessId(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    base::ProcessId,
    base::trace_event::TraceArguments*,
    unsigned int)
{
    return EmptyTraceEventHandle();
}

base::trace_event::TraceEventHandle AddTraceEventWithThreadIdAndTimestamp(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    base::PlatformThreadId,
    const base::TimeTicks&,
    base::trace_event::TraceArguments*,
    unsigned int)
{
    return EmptyTraceEventHandle();
}

base::trace_event::TraceEventHandle AddTraceEventWithThreadIdAndTimestamp(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    uint64_t,
    base::PlatformThreadId,
    const base::TimeTicks&,
    base::trace_event::TraceArguments*,
    unsigned int)
{
    return EmptyTraceEventHandle();
}

base::trace_event::TraceEventHandle AddTraceEventWithThreadIdAndTimestamps(char,
    const unsigned char*,
    const char*,
    const char*,
    uint64_t,
    base::PlatformThreadId,
    const base::TimeTicks&,
    const base::ThreadTicks&,
    unsigned int)
{
    return EmptyTraceEventHandle();
}

void AddMetadataEvent(const unsigned char*,
    const char*,
    base::trace_event::TraceArguments*,
    unsigned int)
{
}

int GetNumTracesRecorded()
{
    return 0;
}

void UpdateTraceEventDuration(const unsigned char*,
    const char*,
    base::trace_event::TraceEventHandle)
{
}

void UpdateTraceEventDurationExplicit(const unsigned char*,
    const char*,
    base::trace_event::TraceEventHandle,
    base::PlatformThreadId,
    bool,
    const base::TimeTicks&,
    const base::ThreadTicks&)
{
}

} // namespace trace_event_internal

namespace perfetto {

TracedDictionary TracedValue::WriteDictionary() &&
{
    return TracedDictionary();
}

TracedArray TracedValue::WriteArray() &&
{
    return TracedArray();
}

TracedArray TracedDictionary::AddArray(StaticString)
{
    return TracedArray();
}

TracedArray TracedDictionary::AddArray(DynamicString)
{
    return TracedArray();
}

TracedDictionary TracedDictionary::AddDictionary(StaticString)
{
    return TracedDictionary();
}

TracedDictionary TracedDictionary::AddDictionary(DynamicString)
{
    return TracedDictionary();
}

TracedArray TracedArray::AppendArray()
{
    return TracedArray();
}

TracedDictionary TracedArray::AppendDictionary()
{
    return TracedDictionary();
}

} // namespace perfetto
