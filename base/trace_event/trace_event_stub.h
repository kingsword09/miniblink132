// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_TRACE_EVENT_STUB_H_
#define BASE_TRACE_EVENT_TRACE_EVENT_STUB_H_

#include <stddef.h>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include "base/base_export.h"
#include "base/memory/scoped_refptr.h"
#include "base/process/process_handle.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/trace_event/common/trace_event_common.h"
#include "base/trace_event/memory_allocator_dump_guid.h"
#include "base/values.h"

#define TRACE_STR_COPY(str) str
#define TRACE_ID_WITH_SCOPE(scope, ...) 0
#define TRACE_ID_GLOBAL(id) 0
#define TRACE_ID_LOCAL(id) 0

namespace trace_event_internal {

const unsigned long long kNoId = 0;

template <typename... Args> void Ignore(Args&&... args)
{
}

struct IgnoredValue {
    template <typename... Args> IgnoredValue(Args&&... args)
    {
    }
};

} // namespace trace_event_internal

#define INTERNAL_TRACE_IGNORE(...) (false ? trace_event_internal::Ignore(__VA_ARGS__) : (void)0)

#define INTERNAL_TRACE_EVENT_UID2(prefix, line) prefix##line
#define INTERNAL_TRACE_EVENT_UID(prefix, line) INTERNAL_TRACE_EVENT_UID2(prefix, line)
#define INTERNAL_TRACE_EVENT_ADD(phase, category_group, name, flags)                                                                                           \
    do {                                                                                                                                                       \
        const unsigned char* trace_event_category_group_enabled = trace_event_internal::GetCategoryGroupEnabled(category_group);                                \
        if (trace_event_category_group_enabled && *trace_event_category_group_enabled) {                                                                        \
            base::trace_event::TraceArguments trace_event_args;                                                                                                 \
            trace_event_internal::AddTraceEvent(phase, trace_event_category_group_enabled, name, nullptr, 0, &trace_event_args, flags);                         \
        }                                                                                                                                                      \
    } while (0)
#define INTERNAL_TRACE_EVENT_SCOPED(category_group, name)                                                                                                      \
    trace_event_internal::ScopedTraceEvent INTERNAL_TRACE_EVENT_UID(trace_event_scope_, __LINE__)(category_group, name)

// Defined in application_state_proto_android.h
#define TRACE_APPLICATION_STATE(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)

#define TRACE_HEAP_PROFILER_API_SCOPED_TASK_EXECUTION trace_event_internal::IgnoredValue

#define TRACE_ID_MANGLE(val) (val)

#define TRACE_EVENT_API_CURRENT_THREAD_ID 0

// Legacy trace macros
#define TRACE_EVENT0(category_group, name) INTERNAL_TRACE_EVENT_SCOPED(category_group, name)
#define TRACE_EVENT_WITH_FLOW0(category_group, name, bind_id, flags) TRACE_EVENT0(category_group, name)
#define TRACE_EVENT1(category_group, name, ...) INTERNAL_TRACE_EVENT_SCOPED(category_group, name)
#define TRACE_EVENT_WITH_FLOW1(category_group, name, bind_id, flags, ...) TRACE_EVENT1(category_group, name)
#define TRACE_EVENT2(category_group, name, ...) INTERNAL_TRACE_EVENT_SCOPED(category_group, name)
#define TRACE_EVENT_WITH_FLOW2(category_group, name, bind_id, flags, ...) TRACE_EVENT2(category_group, name)
#define TRACE_EVENT_INSTANT0(category_group, name, scope) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name, scope)
#define TRACE_EVENT_INSTANT1(category_group, name, scope, ...) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_INSTANT2(category_group, name, scope, ...) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_COPY_INSTANT0(category_group, name, scope) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_COPY_INSTANT1(category_group, name, scope, ...) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_COPY_INSTANT2(category_group, name, scope, ...) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_INSTANT_WITH_FLAGS0(category_group, name, scope, flags) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name, (scope) | (flags))
#define TRACE_EVENT_INSTANT_WITH_FLAGS1(category_group, name, scope, flags, ...) TRACE_EVENT_INSTANT_WITH_FLAGS0(category_group, name, scope, flags)
#define TRACE_EVENT_INSTANT_WITH_TIMESTAMP0(category_group, name, scope, timestamp) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_INSTANT_WITH_TIMESTAMP1(category_group, name, scope, timestamp, ...) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_BEGIN0(category_group, name) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_BEGIN1(category_group, name, ...) TRACE_EVENT_BEGIN0(category_group, name)
#define TRACE_EVENT_BEGIN2(category_group, name, ...) TRACE_EVENT_BEGIN0(category_group, name)
#define TRACE_EVENT_BEGIN_WITH_FLAGS0(category_group, name, flags) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, flags)
#define TRACE_EVENT_BEGIN_WITH_FLAGS1(category_group, name, flags, ...) TRACE_EVENT_BEGIN_WITH_FLAGS0(category_group, name, flags)
#define TRACE_EVENT_COPY_BEGIN2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_BEGIN_WITH_ID_TID_AND_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_BEGIN_WITH_ID_TID_AND_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_BEGIN_WITH_ID_TID_AND_TIMESTAMP1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_BEGIN_WITH_ID_TID_AND_TIMESTAMP2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_END0(category_group, name) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_END1(category_group, name, ...) TRACE_EVENT_END0(category_group, name)
#define TRACE_EVENT_END2(category_group, name, ...) TRACE_EVENT_END0(category_group, name)
#define TRACE_EVENT_END_WITH_FLAGS0(category_group, name, flags) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, flags)
#define TRACE_EVENT_END_WITH_FLAGS1(category_group, name, flags, ...) TRACE_EVENT_END_WITH_FLAGS0(category_group, name, flags)
#define TRACE_EVENT_COPY_END2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_MARK_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_MARK_WITH_TIMESTAMP1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_MARK_WITH_TIMESTAMP2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_MARK(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_MARK1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_MARK_WITH_TIMESTAMP(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_END_WITH_ID_TID_AND_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_END_WITH_ID_TID_AND_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_END_WITH_ID_TID_AND_TIMESTAMP1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_END_WITH_ID_TID_AND_TIMESTAMP2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_COUNTER1(category_group, name, value) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, TRACE_EVENT_FLAG_NONE)
#define TRACE_COUNTER_WITH_FLAG1(category_group, name, value, flags) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, flags)
#define TRACE_COPY_COUNTER1(category_group, name, value) TRACE_COUNTER1(category_group, name, value)
#define TRACE_COUNTER2(category_group, name, ...) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, TRACE_EVENT_FLAG_NONE)
#define TRACE_COPY_COUNTER2(category_group, name, ...) TRACE_COUNTER2(category_group, name)
#define TRACE_COUNTER_WITH_TIMESTAMP1(category_group, name, value, timestamp) TRACE_COUNTER1(category_group, name, value)
#define TRACE_COUNTER_WITH_TIMESTAMP2(category_group, name, ...) TRACE_COUNTER2(category_group, name)
#define TRACE_COUNTER_ID1(category_group, name, id, value) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, TRACE_EVENT_FLAG_HAS_ID)
#define TRACE_COPY_COUNTER_ID1(category_group, name, id, value) TRACE_COUNTER_ID1(category_group, name, id, value)
#define TRACE_COUNTER_ID2(category_group, name, id, ...) TRACE_COUNTER_ID1(category_group, name, id, 0)
#define TRACE_COPY_COUNTER_ID2(category_group, name, id, ...) TRACE_COUNTER_ID2(category_group, name, id)
#define TRACE_EVENT_SAMPLE_WITH_ID1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_BEGIN0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_BEGIN1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_BEGIN2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_ASYNC_BEGIN0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_ASYNC_BEGIN1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_ASYNC_BEGIN2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_ASYNC_BEGIN_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_STEP_INTO0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_STEP_INTO1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_STEP_INTO_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_STEP_PAST0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_STEP_PAST1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_END0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_END1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_END2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_ASYNC_END0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_ASYNC_END1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_ASYNC_END2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_ASYNC_END_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_FLAGS0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_END0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_END1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_END2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_FLAGS0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN_WITH_TTS2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END_WITH_TTS2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP_AND_FLAGS0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP_AND_FLAGS0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN2(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END_WITH_TIMESTAMP0(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_METADATA1(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_CLOCK_SYNC_RECEIVER(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_CLOCK_SYNC_ISSUER(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_OBJECT_CREATED_WITH_ID(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID_AND_TIMESTAMP(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_OBJECT_DELETED_WITH_ID(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)
#define TRACE_EVENT_CATEGORY_GROUP_ENABLED(category_group, ret)                                                                                                \
    do {                                                                                                                                                       \
        *ret = trace_event_internal::CategoryGroupEnabled(category_group);                                                                                      \
    } while (0)
#define TRACE_EVENT_IS_NEW_TRACE(ret)                                                                                                                          \
    do {                                                                                                                                                       \
        *ret = trace_event_internal::GetNumTracesRecorded() > 0;                                                                                               \
    } while (0)

#define TRACE_EVENT_PHASE_BEGIN ('B')
#define TRACE_EVENT_PHASE_END ('E')
#define TRACE_EVENT_PHASE_COMPLETE ('X')
#define TRACE_EVENT_PHASE_INSTANT ('I')
#define TRACE_EVENT_PHASE_ASYNC_BEGIN ('S')
#define TRACE_EVENT_PHASE_ASYNC_STEP_INTO ('T')
#define TRACE_EVENT_PHASE_ASYNC_STEP_PAST ('p')
#define TRACE_EVENT_PHASE_ASYNC_END ('F')
#define TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN ('b')
#define TRACE_EVENT_PHASE_NESTABLE_ASYNC_END ('e')
#define TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT ('n')
#define TRACE_EVENT_PHASE_FLOW_BEGIN ('s')
#define TRACE_EVENT_PHASE_FLOW_STEP ('t')
#define TRACE_EVENT_PHASE_FLOW_END ('f')
#define TRACE_EVENT_PHASE_METADATA ('M')
#define TRACE_EVENT_PHASE_COUNTER ('C')
#define TRACE_EVENT_PHASE_SAMPLE ('P')
#define TRACE_EVENT_PHASE_CREATE_OBJECT ('N')
#define TRACE_EVENT_PHASE_SNAPSHOT_OBJECT ('O')
#define TRACE_EVENT_PHASE_DELETE_OBJECT ('D')
#define TRACE_EVENT_PHASE_MEMORY_DUMP ('v')
#define TRACE_EVENT_PHASE_MARK ('R')
#define TRACE_EVENT_PHASE_CLOCK_SYNC ('c')

#define TRACE_EVENT_FLAG_NONE (static_cast<unsigned int>(0))
#define TRACE_EVENT_FLAG_COPY (static_cast<unsigned int>(1 << 0))
#define TRACE_EVENT_FLAG_HAS_ID (static_cast<unsigned int>(1 << 1))
#define TRACE_EVENT_FLAG_SCOPE_OFFSET (static_cast<unsigned int>(1 << 2))
#define TRACE_EVENT_FLAG_SCOPE_EXTRA (static_cast<unsigned int>(1 << 3))
#define TRACE_EVENT_FLAG_EXPLICIT_TIMESTAMP (static_cast<unsigned int>(1 << 4))
#define TRACE_EVENT_FLAG_ASYNC_TTS (static_cast<unsigned int>(1 << 5))
#define TRACE_EVENT_FLAG_BIND_TO_ENCLOSING (static_cast<unsigned int>(1 << 6))
#define TRACE_EVENT_FLAG_FLOW_IN (static_cast<unsigned int>(1 << 7))
#define TRACE_EVENT_FLAG_FLOW_OUT (static_cast<unsigned int>(1 << 8))
#define TRACE_EVENT_FLAG_HAS_CONTEXT_ID (static_cast<unsigned int>(1 << 9))
#define TRACE_EVENT_FLAG_HAS_PROCESS_ID (static_cast<unsigned int>(1 << 10))
#define TRACE_EVENT_FLAG_HAS_LOCAL_ID (static_cast<unsigned int>(1 << 11))
#define TRACE_EVENT_FLAG_HAS_GLOBAL_ID (static_cast<unsigned int>(1 << 12))
#define TRACE_EVENT_FLAG_JAVA_STRING_LITERALS (static_cast<unsigned int>(1 << 16))
#define TRACE_EVENT_FLAG_SCOPE_MASK (static_cast<unsigned int>(TRACE_EVENT_FLAG_SCOPE_OFFSET | TRACE_EVENT_FLAG_SCOPE_EXTRA))

#define TRACE_VALUE_TYPE_BOOL (static_cast<unsigned char>(1))
#define TRACE_VALUE_TYPE_UINT (static_cast<unsigned char>(2))
#define TRACE_VALUE_TYPE_INT (static_cast<unsigned char>(3))
#define TRACE_VALUE_TYPE_DOUBLE (static_cast<unsigned char>(4))
#define TRACE_VALUE_TYPE_POINTER (static_cast<unsigned char>(5))
#define TRACE_VALUE_TYPE_STRING (static_cast<unsigned char>(6))
#define TRACE_VALUE_TYPE_COPY_STRING (static_cast<unsigned char>(7))
#define TRACE_VALUE_TYPE_CONVERTABLE (static_cast<unsigned char>(8))
#define TRACE_VALUE_TYPE_PROTO (static_cast<unsigned char>(9))

#define TRACE_EVENT_SCOPE_GLOBAL (static_cast<unsigned char>(0 << 2))
#define TRACE_EVENT_SCOPE_PROCESS (static_cast<unsigned char>(1 << 2))
#define TRACE_EVENT_SCOPE_THREAD (static_cast<unsigned char>(2 << 2))
#define TRACE_EVENT_SCOPE_NAME_GLOBAL ('g')
#define TRACE_EVENT_SCOPE_NAME_PROCESS ('p')
#define TRACE_EVENT_SCOPE_NAME_THREAD ('t')

// Typed macros. The lightweight backend records event identity and timing while
// intentionally ignoring Perfetto-specific tracks/debug annotations.
#define TRACE_EVENT_BEGIN(category, name, ...) trace_event_internal::AddTypedTraceEvent(TRACE_EVENT_PHASE_BEGIN, category, name, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_END(category, ...) trace_event_internal::AddTypedTraceEvent(TRACE_EVENT_PHASE_END, category, "TRACE_EVENT_END", TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT(category, name, ...) INTERNAL_TRACE_EVENT_SCOPED(category, name)
#define TRACE_EVENT_INSTANT(category, name, ...) trace_event_internal::AddTypedTraceEvent(TRACE_EVENT_PHASE_INSTANT, category, name, TRACE_EVENT_SCOPE_THREAD)

namespace base {

class SequencedTaskRunner;
class SingleThreadTaskRunner;

namespace trace_event {

#ifndef BASE_TRACE_EVENT_TRACE_EVENT_HANDLE_DEFINED
#define BASE_TRACE_EVENT_TRACE_EVENT_HANDLE_DEFINED
struct TraceEventHandle {
    uint32_t chunk_seq;
    unsigned chunk_index : 26;
    unsigned event_index : 6;
};
#endif

class BASE_EXPORT ConvertableToTraceFormat {
public:
    ConvertableToTraceFormat() = default;
    ConvertableToTraceFormat(const ConvertableToTraceFormat&) = delete;
    ConvertableToTraceFormat& operator=(const ConvertableToTraceFormat&) = delete;
    virtual ~ConvertableToTraceFormat();

    // Append the class info to the provided |out| string. The appended
    // data must be a valid JSON object. Strings must be properly quoted, and
    // escaped. There is no processing applied to the content after it is
    // appended.
    virtual void AppendAsTraceFormat(std::string* out) const = 0;
};

class BASE_EXPORT TracedValue : public ConvertableToTraceFormat {
public:
    class DictionaryWriter {
    public:
        struct Entry {
            template <typename T>
            Entry(const char*, const T&)
            {
            }
        };

        DictionaryWriter(std::initializer_list<Entry>)
        {
        }

        void WriteToValue(TracedValue*)
        {
        }
    };

    explicit TracedValue(size_t capacity = 0)
    {
    }

    static DictionaryWriter Dictionary(std::initializer_list<DictionaryWriter::Entry> entries)
    {
        return DictionaryWriter(entries);
    }

    void EndDictionary()
    {
    }
    void EndArray()
    {
    }

    void SetInteger(const char* name, int value)
    {
    }
    void SetDouble(const char* name, double value)
    {
    }
    void SetBoolean(const char* name, bool value)
    {
    }
    void SetString(const char* name, std::string_view value)
    {
    }
    void SetValue(const char* name, TracedValue* value)
    {
    }
    void BeginDictionary(const char* name)
    {
    }
    void BeginArray(const char* name)
    {
    }

    void SetIntegerWithCopiedName(std::string_view name, int value)
    {
    }
    void SetDoubleWithCopiedName(std::string_view name, double value)
    {
    }
    void SetBooleanWithCopiedName(std::string_view name, bool value)
    {
    }
    void SetStringWithCopiedName(std::string_view name, std::string_view value)
    {
    }
    void SetValueWithCopiedName(std::string_view name, TracedValue* value)
    {
    }
    void BeginDictionaryWithCopiedName(std::string_view name)
    {
    }
    void BeginArrayWithCopiedName(std::string_view name)
    {
    }

    void AppendInteger(int)
    {
    }
    void AppendDouble(double)
    {
    }
    void AppendBoolean(bool)
    {
    }
    void AppendString(std::string_view)
    {
    }
    void BeginArray()
    {
    }
    void BeginDictionary()
    {
    }

    void AppendAsTraceFormat(std::string* out) const override;
};

class BASE_EXPORT TracedValueJSON : public TracedValue {
public:
    explicit TracedValueJSON(size_t capacity = 0)
        : TracedValue(capacity)
    {
    }

    std::unique_ptr<base::Value> ToBaseValue() const
    {
        return nullptr;
    }
    std::string ToJSON() const
    {
        return "";
    }
    std::string ToFormattedJSON() const
    {
        return "";
    }
};

struct MemoryDumpArgs;
class ProcessMemoryDump;

class BASE_EXPORT MemoryDumpProvider {
public:
    MemoryDumpProvider(const MemoryDumpProvider&) = delete;
    MemoryDumpProvider& operator=(const MemoryDumpProvider&) = delete;
    virtual ~MemoryDumpProvider();

    struct Options { };

    virtual bool OnMemoryDump(const MemoryDumpArgs& args, ProcessMemoryDump* pmd) = 0;

protected:
    MemoryDumpProvider() = default;
};

class BASE_EXPORT MemoryDumpManager {
public:
    static constexpr const char* const kTraceCategory = TRACE_DISABLED_BY_DEFAULT("memory-infra");

    static MemoryDumpManager* GetInstance();

    void RegisterDumpProvider(MemoryDumpProvider* mdp,
        const char* name,
        scoped_refptr<SingleThreadTaskRunner> task_runner);
    void RegisterDumpProvider(MemoryDumpProvider* mdp,
        const char* name,
        scoped_refptr<SingleThreadTaskRunner> task_runner,
        MemoryDumpProvider::Options options);
    void RegisterDumpProviderWithSequencedTaskRunner(MemoryDumpProvider* mdp,
        const char* name,
        scoped_refptr<SequencedTaskRunner> task_runner,
        MemoryDumpProvider::Options options);
    void UnregisterDumpProvider(MemoryDumpProvider* mdp);
};

inline uint64_t GetNextGlobalTraceId()
{
    return 0;
}

union BASE_EXPORT TraceValue {
    bool as_bool;
    unsigned long long as_uint;
    long long as_int;
    double as_double;
    // This field is not a raw_ptr<> because it was filtered by the rewriter for:
    // #union
    RAW_PTR_EXCLUSION const void* as_pointer;
    const char* as_string;
    // This field is not a raw_ptr<> because it was filtered by the rewriter for:
    // #union
    RAW_PTR_EXCLUSION ConvertableToTraceFormat* as_convertable;
    // This field is not a raw_ptr<> because it was filtered by the rewriter for:
    // #union
#if 0 // BUILDFLAG(USE_PERFETTO_CLIENT_LIBRARY)
  RAW_PTR_EXCLUSION protozero::HeapBuffered<perfetto::protos::pbzero::DebugAnnotation>* as_proto;
#endif

    // Static method to create a new TraceValue instance from a given
    // initialization value. Note that this deduces the TRACE_VALUE_TYPE_XXX
    // type but doesn't return it, use ForType<T>::value for this.
    //
    // Usage example:
    //     auto v = TraceValue::Make(100);
    //     auto v2 = TraceValue::Make("Some text string");
    //
    // IMPORTANT: Experience shows that the compiler generates worse code when
    // using this method rather than calling Init() directly on an existing
    // TraceValue union :-(
    //
    template <typename T> static TraceValue Make(T&& value)
    {
        TraceValue ret;
        *(int*)1 = 1;
        //ret.Init(std::forward<T>(value));
        return ret;
    }

    // Output current value as a JSON string. |type| must be a valid
    // TRACE_VALUE_TYPE_XXX value.
    void AppendAsJSON(unsigned char type, std::string* out) const;

    // Output current value as a string. If the output string is to be used
    // in a JSON format use AppendAsJSON instead. |type| must be valid
    // TRACE_VALUE_TYPE_XXX value.
    void AppendAsString(unsigned char type, std::string* out) const;

private:
    void Append(unsigned char type, bool as_json, std::string* out) const;
};

class BASE_EXPORT TraceArguments {
public:
    // Maximum number of arguments held by this structure.
    static constexpr size_t kMaxSize = 2;

    // Default constructor, no arguments.
    TraceArguments()
        : size_(0)
    {
    }

    // Constructor for a single argument.
    template <typename T /*, class = decltype(TraceValue::TypeCheck<T>::value)*/>
    TraceArguments(const char* arg1_name, T&& arg1_value)
        : size_(1)
    {
        //     types_[0] = TraceValue::TypeFor<T>::value;
        //     names_[0] = arg1_name;
        //     values_[0].Init(std::forward<T>(arg1_value));
    }

    // Constructor for two arguments.
    template <typename T1, typename T2
        /*, class = decltype(TraceValue::TypeCheck<T1>::value&& TraceValue::TypeCheck<T2>::value)*/>
    TraceArguments(const char* arg1_name, T1&& arg1_value, const char* arg2_name, T2&& arg2_value)
        : size_(2)
    {
        //     types_[0] = TraceValue::TypeFor<T1>::value;
        //     types_[1] = TraceValue::TypeFor<T2>::value;
        //     names_[0] = arg1_name;
        //     names_[1] = arg2_name;
        //     values_[0].Init(std::forward<T1>(arg1_value));
        //     values_[1].Init(std::forward<T2>(arg2_value));
    }

    // Constructor used to convert a legacy set of arguments when there
    // are no convertable values at all.
    TraceArguments(int num_args, const char* const* arg_names, const unsigned char* arg_types, const unsigned long long* arg_values);

    // Constructor used to convert legacy set of arguments, where the
    // convertable values are also provided by an array of CONVERTABLE_TYPE.
    template <typename CONVERTABLE_TYPE>
    TraceArguments(
        int num_args, const char* const* arg_names, const unsigned char* arg_types, const unsigned long long* arg_values, CONVERTABLE_TYPE* arg_convertables)
    {
        //     static int max_args = static_cast<int>(kMaxSize);
        //     if (num_args > max_args)
        //       num_args = max_args;
        //     size_ = static_cast<unsigned char>(num_args);
        //     for (size_t n = 0; n < size_; ++n) {
        //       types_[n] = arg_types[n];
        //       names_[n] = arg_names[n];
        //       if (arg_types[n] == TRACE_VALUE_TYPE_CONVERTABLE) {
        //         values_[n].Init(
        //           std::forward<CONVERTABLE_TYPE>(std::move(arg_convertables[n])));
        //       }
        //       else {
        //         values_[n].as_uint = arg_values[n];
        //       }
        //     }
    }

    // Destructor. NOTE: Intentionally inlined (see note above).
    ~TraceArguments()
    {
        //     for (size_t n = 0; n < size_; ++n) {
        //       if (types_[n] == TRACE_VALUE_TYPE_CONVERTABLE)
        //         delete values_[n].as_convertable;
        //       if (types_[n] == TRACE_VALUE_TYPE_PROTO)
        //         delete values_[n].as_proto;
        //     }
    }

    // Disallow copy operations.
    TraceArguments(const TraceArguments&) = delete;
    TraceArguments& operator=(const TraceArguments&) = delete;

    // Allow move operations.
    TraceArguments(TraceArguments&& other) noexcept
    {
        ::memcpy(this, &other, sizeof(*this));
        // All owning pointers were copied to |this|. Setting |other.size_| will
        // mask the pointer values still in |other|.
        other.size_ = 0;
    }

    TraceArguments& operator=(TraceArguments&&) noexcept;

    // Accessors
    size_t size() const
    {
        return size_;
    }
    const unsigned char* types() const
    {
        return types_;
    }
    const char* const* names() const
    {
        return names_;
    }
    //const TraceValue* values() const { return values_; }

    // Reset to empty arguments list.
    void Reset();

    // Use |storage| to copy all copyable strings.
    // If |copy_all_strings| is false, then only the TRACE_VALUE_TYPE_COPY_STRING
    // values will be copied into storage. If it is true, then argument names are
    // also copied to storage, as well as the strings pointed to by
    // |*extra_string1| and |*extra_string2|.
    // NOTE: If there are no strings to copy, |*storage| is left untouched.
    //   void CopyStringsTo(StringStorage* storage,
    //     bool copy_all_strings,
    //     const char** extra_string1,
    //     const char** extra_string2);

    // Append debug string representation to |*out|.
    void AppendDebugString(std::string* out);

private:
    unsigned char size_;
    unsigned char types_[kMaxSize];
    const char* names_[kMaxSize];
    //TraceValue values_[kMaxSize];
};

} // namespace trace_event
} // namespace base

namespace trace_event_internal {

const unsigned char* GetCategoryGroupEnabled(const char* category_group);
bool CategoryGroupEnabled(const char* category_group);

base::trace_event::TraceEventHandle AddTraceEvent(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    const char* scope,
    uint64_t id,
    base::trace_event::TraceArguments* args,
    unsigned int flags);
base::trace_event::TraceEventHandle AddTraceEventWithBindId(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    const char* scope,
    uint64_t id,
    uint64_t bind_id,
    base::trace_event::TraceArguments* args,
    unsigned int flags);
base::trace_event::TraceEventHandle AddTraceEventWithProcessId(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    const char* scope,
    uint64_t id,
    base::ProcessId process_id,
    base::trace_event::TraceArguments* args,
    unsigned int flags);
base::trace_event::TraceEventHandle AddTraceEventWithThreadIdAndTimestamp(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    const char* scope,
    uint64_t id,
    base::PlatformThreadId thread_id,
    const base::TimeTicks& timestamp,
    base::trace_event::TraceArguments* args,
    unsigned int flags);
base::trace_event::TraceEventHandle AddTraceEventWithThreadIdAndTimestamp(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    const char* scope,
    uint64_t id,
    uint64_t bind_id,
    base::PlatformThreadId thread_id,
    const base::TimeTicks& timestamp,
    base::trace_event::TraceArguments* args,
    unsigned int flags);
base::trace_event::TraceEventHandle AddTraceEventWithThreadIdAndTimestamps(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    const char* scope,
    uint64_t id,
    uint64_t bind_id,
    base::PlatformThreadId thread_id,
    const base::TimeTicks& timestamp,
    const base::ThreadTicks& thread_timestamp,
    base::trace_event::TraceArguments* args,
    unsigned int flags);
void AddMetadataEvent(const unsigned char* category_group_enabled,
    const char* name,
    base::trace_event::TraceArguments* args,
    unsigned int flags);
int GetNumTracesRecorded();
void UpdateTraceEventDuration(const unsigned char* category_group_enabled,
    const char* name,
    base::trace_event::TraceEventHandle handle);
void UpdateTraceEventDurationExplicit(const unsigned char* category_group_enabled,
    const char* name,
    base::trace_event::TraceEventHandle handle,
    base::PlatformThreadId thread_id,
    bool explicit_timestamps,
    const base::TimeTicks& now,
    const base::ThreadTicks& thread_now);

class ScopedTraceEvent {
public:
    ScopedTraceEvent(const char* category_group, const char* name);
    template <typename Category, typename Name>
    ScopedTraceEvent(const Category& category_group, const Name& name);
    ScopedTraceEvent(const ScopedTraceEvent&) = delete;
    ScopedTraceEvent& operator=(const ScopedTraceEvent&) = delete;
    ~ScopedTraceEvent();

private:
    const unsigned char* category_group_enabled_ = nullptr;
    const char* name_ = nullptr;
    std::string name_storage_;
    base::trace_event::TraceEventHandle handle_ {};
};

} // namespace trace_event_internal

// Stub implementation for
// perfetto::StaticString/ThreadTrack/TracedValue/TracedDictionary/TracedArray/
// Track.
namespace perfetto {

class TracedArray;
class TracedDictionary;
class EventContext;

class StaticString {
public:
    StaticString()
        : value_("")
    {
    }
    StaticString(const char* value)
        : value_(value ? value : "")
    {
    }
    template <size_t N>
    StaticString(const char (&value)[N])
        : value_(value)
    {
    }
    template <typename T>
    StaticString(T)
        : value_("")
    {
    }
    const char* c_str() const { return value_; }

private:
    const char* value_;
};

class DynamicString {
public:
    DynamicString()
        : value_()
    {
    }
    explicit DynamicString(const char* value)
        : value_(value ? value : "")
    {
    }
    explicit DynamicString(const std::string& value)
        : value_(value)
    {
    }
    template <typename T>
    explicit DynamicString(T)
        : value_("")
    {
    }
    const char* c_str() const { return value_.c_str(); }

private:
    std::string value_;
};

class TracedValue {
public:
    void WriteInt64(int64_t) &&
    {
    }
    void WriteUInt64(uint64_t) &&
    {
    }
    void WriteDouble(double) &&
    {
    }
    void WriteBoolean(bool) &&
    {
    }
    void WriteString(const char*) &&
    {
    }
    void WriteString(const char*, size_t) &&
    {
    }
    void WriteString(const std::string&) &&
    {
    }
    void WritePointer(const void*) &&
    {
    }

    TracedDictionary WriteDictionary() &&;
    TracedArray WriteArray() &&;
};

class TracedDictionary {
public:
    TracedValue AddItem(StaticString)
    {
        return TracedValue();
    }
    TracedValue AddItem(DynamicString)
    {
        return TracedValue();
    }

    template <typename T> void Add(StaticString, T&&)
    {
    }
    template <typename T> void Add(DynamicString, T&&)
    {
    }

    TracedDictionary AddDictionary(StaticString);
    TracedDictionary AddDictionary(DynamicString);
    TracedArray AddArray(StaticString);
    TracedArray AddArray(DynamicString);
};

class TracedArray {
public:
    TracedValue AppendItem()
    {
        return TracedValue();
    }

    template <typename T> void Append(T&&)
    {
    }

    TracedDictionary AppendDictionary();
    TracedArray AppendArray();
};

template <class T> void WriteIntoTracedValue(TracedValue, T&&)
{
}

template <class T> void WriteIntoTracedValueWithFallback(TracedValue, T&&, const char*)
{
}

struct Track {
    explicit Track(uint64_t id)
    {
    }
};

struct NamedTrack {
    NamedTrack() = default;
    template <class T> explicit NamedTrack(T name, uint64_t id = 0, Track parent = Track { 0 })
    {
    }
};

namespace protos::pbzero {
namespace SequenceManagerTask {

enum class QueueName {
    UNKNOWN_TQ = 0,
    DEFAULT_TQ = 1,
    TASK_ENVIRONMENT_DEFAULT_TQ = 2,
    TEST2_TQ = 3,
    TEST_TQ = 4,
    CONTROL_TQ = 5,
    SUBTHREAD_CONTROL_TQ = 6,
    SUBTHREAD_DEFAULT_TQ = 7,
    SUBTHREAD_INPUT_TQ = 8,
    UI_BEST_EFFORT_TQ = 9,
    UI_BOOTSTRAP_TQ = 10,
    UI_CONTROL_TQ = 11,
    UI_DEFAULT_TQ = 12,
    UI_NAVIGATION_NETWORK_RESPONSE_TQ = 13,
    UI_RUN_ALL_PENDING_TQ = 14,
    UI_SERVICE_WORKER_STORAGE_CONTROL_RESPONSE_TQ = 15,
    UI_THREAD_TQ = 16,
    UI_USER_BLOCKING_TQ = 17,
    UI_USER_INPUT_TQ = 18,
    UI_USER_VISIBLE_TQ = 19,
    IO_BEST_EFFORT_TQ = 20,
    IO_BOOTSTRAP_TQ = 21,
    IO_CONTROL_TQ = 22,
    IO_DEFAULT_TQ = 23,
    IO_NAVIGATION_NETWORK_RESPONSE_TQ = 24,
    IO_RUN_ALL_PENDING_TQ = 25,
    IO_SERVICE_WORKER_STORAGE_CONTROL_RESPONSE_TQ = 26,
    IO_THREAD_TQ = 27,
    IO_USER_BLOCKING_TQ = 28,
    IO_USER_INPUT_TQ = 29,
    IO_USER_VISIBLE_TQ = 30,
    COMPOSITOR_TQ = 31,
    DETACHED_TQ = 32,
    FRAME_DEFERRABLE_TQ = 33,
    FRAME_LOADING_CONTROL_TQ = 34,
    FRAME_LOADING_TQ = 35,
    FRAME_PAUSABLE_TQ = 36,
    FRAME_THROTTLEABLE_TQ = 37,
    FRAME_UNPAUSABLE_TQ = 38,
    IDLE_TQ = 39,
    INPUT_TQ = 40,
    IPC_TRACKING_FOR_CACHED_PAGES_TQ = 41,
    NON_WAKING_TQ = 42,
    OTHER_TQ = 43,
    V8_TQ = 44,
    WEB_SCHEDULING_TQ = 45,
    WORKER_IDLE_TQ = 46,
    WORKER_PAUSABLE_TQ = 47,
    WORKER_THREAD_INTERNAL_TQ = 48,
    WORKER_THROTTLEABLE_TQ = 49,
    WORKER_UNPAUSABLE_TQ = 50,
    WORKER_WEB_SCHEDULING_TQ = 51,
    UI_USER_BLOCKING_DEFERRABLE_TQ = 52,
    IO_USER_BLOCKING_DEFERRABLE_TQ = 53,
    UI_BEFORE_UNLOAD_BROWSER_RESPONSE_TQ = 54,
    IO_BEFORE_UNLOAD_BROWSER_RESPONSE_TQ = 55,
    V8_USER_VISIBLE_TQ = 56,
    V8_BEST_EFFORT_TQ = 57,
    V8_LOW_PRIORITY_TQ = 56,
};

// inline const char* QueueName_Name(QueueName value) {
//   switch (value) {
//     case QueueName::UNKNOWN_TQ:
//       return "UNKNOWN_TQ";
//     case QueueName::DEFAULT_TQ:
//       return "DEFAULT_TQ";
//     case QueueName::TASK_ENVIRONMENT_DEFAULT_TQ:
//       return "TASK_ENVIRONMENT_DEFAULT_TQ";
//     case QueueName::TEST2_TQ:
//       return "TEST2_TQ";
//     case QueueName::TEST_TQ:
//       return "TEST_TQ";
//   }
//   return "TEST_TQ";
// }

inline const char* QueueName_Name(QueueName value)
{
    switch (value) {
    case QueueName::UNKNOWN_TQ:
        return "UNKNOWN_TQ";

    case QueueName::DEFAULT_TQ:
        return "DEFAULT_TQ";

    case QueueName::TASK_ENVIRONMENT_DEFAULT_TQ:
        return "TASK_ENVIRONMENT_DEFAULT_TQ";

    case QueueName::TEST2_TQ:
        return "TEST2_TQ";

    case QueueName::TEST_TQ:
        return "TEST_TQ";

    case QueueName::CONTROL_TQ:
        return "CONTROL_TQ";

    case QueueName::SUBTHREAD_CONTROL_TQ:
        return "SUBTHREAD_CONTROL_TQ";

    case QueueName::SUBTHREAD_DEFAULT_TQ:
        return "SUBTHREAD_DEFAULT_TQ";

    case QueueName::SUBTHREAD_INPUT_TQ:
        return "SUBTHREAD_INPUT_TQ";

    case QueueName::UI_BEST_EFFORT_TQ:
        return "UI_BEST_EFFORT_TQ";

    case QueueName::UI_BOOTSTRAP_TQ:
        return "UI_BOOTSTRAP_TQ";

    case QueueName::UI_CONTROL_TQ:
        return "UI_CONTROL_TQ";

    case QueueName::UI_DEFAULT_TQ:
        return "UI_DEFAULT_TQ";

    case QueueName::UI_NAVIGATION_NETWORK_RESPONSE_TQ:
        return "UI_NAVIGATION_NETWORK_RESPONSE_TQ";

    case QueueName::UI_RUN_ALL_PENDING_TQ:
        return "UI_RUN_ALL_PENDING_TQ";

    case QueueName::UI_SERVICE_WORKER_STORAGE_CONTROL_RESPONSE_TQ:
        return "UI_SERVICE_WORKER_STORAGE_CONTROL_RESPONSE_TQ";

    case QueueName::UI_THREAD_TQ:
        return "UI_THREAD_TQ";

    case QueueName::UI_USER_BLOCKING_TQ:
        return "UI_USER_BLOCKING_TQ";

    case QueueName::UI_USER_INPUT_TQ:
        return "UI_USER_INPUT_TQ";

    case QueueName::UI_USER_VISIBLE_TQ:
        return "UI_USER_VISIBLE_TQ";

    case QueueName::IO_BEST_EFFORT_TQ:
        return "IO_BEST_EFFORT_TQ";

    case QueueName::IO_BOOTSTRAP_TQ:
        return "IO_BOOTSTRAP_TQ";

    case QueueName::IO_CONTROL_TQ:
        return "IO_CONTROL_TQ";

    case QueueName::IO_DEFAULT_TQ:
        return "IO_DEFAULT_TQ";

    case QueueName::IO_NAVIGATION_NETWORK_RESPONSE_TQ:
        return "IO_NAVIGATION_NETWORK_RESPONSE_TQ";

    case QueueName::IO_RUN_ALL_PENDING_TQ:
        return "IO_RUN_ALL_PENDING_TQ";

    case QueueName::IO_SERVICE_WORKER_STORAGE_CONTROL_RESPONSE_TQ:
        return "IO_SERVICE_WORKER_STORAGE_CONTROL_RESPONSE_TQ";

    case QueueName::IO_THREAD_TQ:
        return "IO_THREAD_TQ";

    case QueueName::IO_USER_BLOCKING_TQ:
        return "IO_USER_BLOCKING_TQ";

    case QueueName::IO_USER_INPUT_TQ:
        return "IO_USER_INPUT_TQ";

    case QueueName::IO_USER_VISIBLE_TQ:
        return "IO_USER_VISIBLE_TQ";

    case QueueName::COMPOSITOR_TQ:
        return "COMPOSITOR_TQ";

    case QueueName::DETACHED_TQ:
        return "DETACHED_TQ";

    case QueueName::FRAME_DEFERRABLE_TQ:
        return "FRAME_DEFERRABLE_TQ";

    case QueueName::FRAME_LOADING_CONTROL_TQ:
        return "FRAME_LOADING_CONTROL_TQ";

    case QueueName::FRAME_LOADING_TQ:
        return "FRAME_LOADING_TQ";

    case QueueName::FRAME_PAUSABLE_TQ:
        return "FRAME_PAUSABLE_TQ";

    case QueueName::FRAME_THROTTLEABLE_TQ:
        return "FRAME_THROTTLEABLE_TQ";

    case QueueName::FRAME_UNPAUSABLE_TQ:
        return "FRAME_UNPAUSABLE_TQ";

    case QueueName::IDLE_TQ:
        return "IDLE_TQ";

    case QueueName::INPUT_TQ:
        return "INPUT_TQ";

    case QueueName::IPC_TRACKING_FOR_CACHED_PAGES_TQ:
        return "IPC_TRACKING_FOR_CACHED_PAGES_TQ";

    case QueueName::NON_WAKING_TQ:
        return "NON_WAKING_TQ";

    case QueueName::OTHER_TQ:
        return "OTHER_TQ";

    case QueueName::V8_TQ:
        return "V8_TQ";

    case QueueName::WEB_SCHEDULING_TQ:
        return "WEB_SCHEDULING_TQ";

    case QueueName::WORKER_IDLE_TQ:
        return "WORKER_IDLE_TQ";

    case QueueName::WORKER_PAUSABLE_TQ:
        return "WORKER_PAUSABLE_TQ";

    case QueueName::WORKER_THREAD_INTERNAL_TQ:
        return "WORKER_THREAD_INTERNAL_TQ";

    case QueueName::WORKER_THROTTLEABLE_TQ:
        return "WORKER_THROTTLEABLE_TQ";

    case QueueName::WORKER_UNPAUSABLE_TQ:
        return "WORKER_UNPAUSABLE_TQ";

    case QueueName::WORKER_WEB_SCHEDULING_TQ:
        return "WORKER_WEB_SCHEDULING_TQ";

    case QueueName::UI_USER_BLOCKING_DEFERRABLE_TQ:
        return "UI_USER_BLOCKING_DEFERRABLE_TQ";

    case QueueName::IO_USER_BLOCKING_DEFERRABLE_TQ:
        return "IO_USER_BLOCKING_DEFERRABLE_TQ";

    case QueueName::UI_BEFORE_UNLOAD_BROWSER_RESPONSE_TQ:
        return "UI_BEFORE_UNLOAD_BROWSER_RESPONSE_TQ";

    case QueueName::IO_BEFORE_UNLOAD_BROWSER_RESPONSE_TQ:
        return "IO_BEFORE_UNLOAD_BROWSER_RESPONSE_TQ";

    case QueueName::V8_BEST_EFFORT_TQ:
        return "V8_BEST_EFFORT_TQ";
    case QueueName::V8_LOW_PRIORITY_TQ:
        return "V8_LOW_PRIORITY_TQ";
    }
    return "PBZERO_UNKNOWN_ENUM_VALUE";
}

} // namespace SequenceManagerTask

namespace ChromeProcessDescriptor {

enum ProcessType {};

} // namespace ChromeProcessDescriptor

} // namespace protos::pbzero
} // namespace perfetto

namespace trace_event_internal {

inline const char* TraceCStringOrNull(const char* value)
{
    return value;
}

inline const char* TraceCStringOrNull(std::nullptr_t)
{
    return nullptr;
}

inline const char* TraceCStringOrNull(const std::string& value)
{
    return value.c_str();
}

inline const char* TraceCStringOrNull(const perfetto::StaticString& value)
{
    return value.c_str();
}

inline const char* TraceCStringOrNull(const perfetto::DynamicString& value)
{
    return value.c_str();
}

template <typename T>
const char* TraceCStringOrNull(const T&)
{
    return nullptr;
}

template <typename T>
std::string TraceStringCopy(const T& value)
{
    const char* ptr = TraceCStringOrNull(value);
    return ptr ? std::string(ptr) : std::string();
}

template <typename Category, typename Name>
void AddTypedTraceEvent(char phase, const Category& category_group, const Name& name, unsigned int flags)
{
    std::string category_storage = TraceStringCopy(category_group);
    std::string name_storage = TraceStringCopy(name);
    const unsigned char* category_group_enabled = GetCategoryGroupEnabled(category_storage.c_str());
    if (!category_group_enabled || !*category_group_enabled)
        return;

    base::trace_event::TraceArguments args;
    AddTraceEvent(phase, category_group_enabled, name_storage.c_str(), nullptr, 0, &args, flags);
}

template <typename Category, typename Name>
ScopedTraceEvent::ScopedTraceEvent(const Category& category_group, const Name& name)
{
    std::string category_storage = TraceStringCopy(category_group);
    name_storage_ = TraceStringCopy(name);
    category_group_enabled_ = GetCategoryGroupEnabled(category_storage.c_str());
    if (!category_group_enabled_ || !*category_group_enabled_)
        return;

    name_ = name_storage_.c_str();
    base::trace_event::TraceArguments args;
    handle_ = AddTraceEvent(TRACE_EVENT_PHASE_BEGIN, category_group_enabled_, name_,
        nullptr, 0, &args, TRACE_EVENT_FLAG_NONE);
}

} // namespace trace_event_internal

#endif // BASE_TRACE_EVENT_TRACE_EVENT_STUB_H_
