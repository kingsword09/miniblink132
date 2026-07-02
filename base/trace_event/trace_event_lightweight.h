// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_TRACE_EVENT_LIGHTWEIGHT_H_
#define BASE_TRACE_EVENT_TRACE_EVENT_LIGHTWEIGHT_H_

#include <stddef.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "base/base_export.h"
#include "base/memory/scoped_refptr.h"
#include "base/process/process_handle.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/trace_event/common/trace_event_common.h"
#include "base/trace_event/memory_allocator_dump_guid.h"
#include "base/values.h"

#define TRACE_STR_COPY(str) base::trace_event::TraceStringWithCopy(str)
#define TRACE_ID_WITH_SCOPE(scope, id) trace_event_internal::TraceID::WithScope(scope, id)
#define TRACE_ID_GLOBAL(id) trace_event_internal::TraceID::GlobalId(id)
#define TRACE_ID_LOCAL(id) trace_event_internal::TraceID::LocalId(id)

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

#ifndef TRACE_EVENT_INTERNAL_TRACE_ID_DEFINED
#define TRACE_EVENT_INTERNAL_TRACE_ID_DEFINED

class TraceID {
public:
    class LocalId {
    public:
        explicit LocalId(const void* raw_id)
            : raw_id_(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(raw_id)))
        {
        }
        explicit LocalId(uint64_t raw_id)
            : raw_id_(raw_id)
        {
        }
        uint64_t raw_id() const { return raw_id_; }

    private:
        uint64_t raw_id_;
    };

    class GlobalId {
    public:
        explicit GlobalId(uint64_t raw_id)
            : raw_id_(raw_id)
        {
        }
        uint64_t raw_id() const { return raw_id_; }

    private:
        uint64_t raw_id_;
    };

    class WithScope {
    public:
        WithScope(const char* scope, uint64_t raw_id)
            : scope_(scope)
            , raw_id_(raw_id)
        {
        }
        WithScope(const char* scope, LocalId local_id)
            : scope_(scope)
            , raw_id_(local_id.raw_id())
            , id_flags_(TRACE_EVENT_FLAG_HAS_LOCAL_ID)
        {
        }
        WithScope(const char* scope, GlobalId global_id)
            : scope_(scope)
            , raw_id_(global_id.raw_id())
            , id_flags_(TRACE_EVENT_FLAG_HAS_GLOBAL_ID)
        {
        }
        uint64_t raw_id() const { return raw_id_; }
        const char* scope() const { return scope_; }
        unsigned int id_flags() const { return id_flags_; }

    private:
        const char* scope_ = nullptr;
        uint64_t raw_id_;
        unsigned int id_flags_ = TRACE_EVENT_FLAG_HAS_ID;
    };

    explicit TraceID(const void* raw_id)
        : raw_id_(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(raw_id)))
        , id_flags_(TRACE_EVENT_FLAG_HAS_LOCAL_ID)
    {
    }
    explicit TraceID(unsigned long long raw_id)
        : raw_id_(static_cast<uint64_t>(raw_id))
    {
    }
    explicit TraceID(unsigned long raw_id)
        : raw_id_(static_cast<uint64_t>(raw_id))
    {
    }
    explicit TraceID(unsigned int raw_id)
        : raw_id_(static_cast<uint64_t>(raw_id))
    {
    }
    explicit TraceID(unsigned short raw_id)
        : raw_id_(static_cast<uint64_t>(raw_id))
    {
    }
    explicit TraceID(unsigned char raw_id)
        : raw_id_(static_cast<uint64_t>(raw_id))
    {
    }
    explicit TraceID(long long raw_id)
        : raw_id_(static_cast<uint64_t>(raw_id))
    {
    }
    explicit TraceID(long raw_id)
        : raw_id_(static_cast<uint64_t>(raw_id))
    {
    }
    explicit TraceID(int raw_id)
        : raw_id_(static_cast<uint64_t>(raw_id))
    {
    }
    explicit TraceID(short raw_id)
        : raw_id_(static_cast<uint64_t>(raw_id))
    {
    }
    explicit TraceID(signed char raw_id)
        : raw_id_(static_cast<uint64_t>(raw_id))
    {
    }
    explicit TraceID(LocalId local_id)
        : raw_id_(local_id.raw_id())
        , id_flags_(TRACE_EVENT_FLAG_HAS_LOCAL_ID)
    {
    }
    explicit TraceID(GlobalId global_id)
        : raw_id_(global_id.raw_id())
        , id_flags_(TRACE_EVENT_FLAG_HAS_GLOBAL_ID)
    {
    }
    explicit TraceID(WithScope scoped_id)
        : scope_(scoped_id.scope())
        , raw_id_(scoped_id.raw_id())
        , id_flags_(scoped_id.id_flags())
    {
    }

    uint64_t raw_id() const { return raw_id_; }
    const char* scope() const { return scope_; }
    unsigned int id_flags() const { return id_flags_; }

private:
    const char* scope_ = nullptr;
    uint64_t raw_id_;
    unsigned int id_flags_ = TRACE_EVENT_FLAG_HAS_ID;
};

#endif // TRACE_EVENT_INTERNAL_TRACE_ID_DEFINED

} // namespace trace_event_internal

#define INTERNAL_TRACE_IGNORE(...) (false ? trace_event_internal::Ignore(__VA_ARGS__) : (void)0)

#define INTERNAL_TRACE_EVENT_UID2(prefix, line) prefix##line
#define INTERNAL_TRACE_EVENT_UID(prefix, line) INTERNAL_TRACE_EVENT_UID2(prefix, line)
#define INTERNAL_TRACE_EVENT_ADD(phase, category_group, name, flags, ...)                                                                                       \
    do {                                                                                                                                                       \
        const unsigned char* trace_event_category_group_enabled = trace_event_internal::GetCategoryGroupEnabled(category_group);                                \
        if (trace_event_category_group_enabled && *trace_event_category_group_enabled) {                                                                        \
            base::trace_event::TraceArguments trace_event_args { __VA_ARGS__ };                                                                                 \
            trace_event_internal::AddTraceEvent(phase, trace_event_category_group_enabled, name, nullptr, 0, &trace_event_args, flags);                         \
        }                                                                                                                                                      \
    } while (0)
#define INTERNAL_TRACE_EVENT_ADD_WITH_ID(phase, category_group, name, id, flags, ...)                                                                           \
    do {                                                                                                                                                       \
        const unsigned char* trace_event_category_group_enabled = trace_event_internal::GetCategoryGroupEnabled(category_group);                                \
        if (trace_event_category_group_enabled && *trace_event_category_group_enabled) {                                                                        \
            unsigned int trace_event_flags = (flags) | TRACE_EVENT_FLAG_HAS_ID;                                                                                 \
            trace_event_internal::TraceID trace_event_id { id };                                                                                                \
            trace_event_flags |= trace_event_id.id_flags();                                                                                                     \
            base::trace_event::TraceArguments trace_event_args { __VA_ARGS__ };                                                                                 \
            trace_event_internal::AddTraceEvent(phase, trace_event_category_group_enabled, name, trace_event_id.scope(),                                        \
                trace_event_id.raw_id(), &trace_event_args, trace_event_flags);                                                                                 \
        }                                                                                                                                                      \
    } while (0)
#define INTERNAL_TRACE_EVENT_ADD_WITH_BIND_ID(phase, category_group, name, bind_id, flags, ...)                                                                \
    do {                                                                                                                                                       \
        const unsigned char* trace_event_category_group_enabled = trace_event_internal::GetCategoryGroupEnabled(category_group);                                \
        if (trace_event_category_group_enabled && *trace_event_category_group_enabled) {                                                                        \
            unsigned int trace_event_flags = (flags);                                                                                                          \
            trace_event_internal::TraceID trace_event_bind_id { bind_id };                                                                                      \
            trace_event_flags |= trace_event_bind_id.id_flags();                                                                                                \
            base::trace_event::TraceArguments trace_event_args { __VA_ARGS__ };                                                                                 \
            trace_event_internal::AddTraceEventWithBindId(phase, trace_event_category_group_enabled, name, nullptr, 0,                                          \
                trace_event_bind_id.raw_id(), &trace_event_args, trace_event_flags);                                                                            \
        }                                                                                                                                                      \
    } while (0)
#define INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(phase, category_group, name, timestamp, flags, ...)                                                             \
    do {                                                                                                                                                       \
        const unsigned char* trace_event_category_group_enabled = trace_event_internal::GetCategoryGroupEnabled(category_group);                                \
        if (trace_event_category_group_enabled && *trace_event_category_group_enabled) {                                                                        \
            base::trace_event::TraceArguments trace_event_args { __VA_ARGS__ };                                                                                 \
            trace_event_internal::AddTraceEventWithThreadIdAndTimestamp(phase, trace_event_category_group_enabled, name, nullptr, 0,                            \
                TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, &trace_event_args, flags);                                                                        \
        }                                                                                                                                                      \
    } while (0)
#define INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(phase, category_group, name, id, thread_id, timestamp, flags, ...)                                  \
    do {                                                                                                                                                       \
        const unsigned char* trace_event_category_group_enabled = trace_event_internal::GetCategoryGroupEnabled(category_group);                                \
        if (trace_event_category_group_enabled && *trace_event_category_group_enabled) {                                                                        \
            unsigned int trace_event_flags = (flags) | TRACE_EVENT_FLAG_HAS_ID;                                                                                 \
            trace_event_internal::TraceID trace_event_id { id };                                                                                                \
            trace_event_flags |= trace_event_id.id_flags();                                                                                                     \
            base::trace_event::TraceArguments trace_event_args { __VA_ARGS__ };                                                                                 \
            trace_event_internal::AddTraceEventWithThreadIdAndTimestamp(phase, trace_event_category_group_enabled, name, trace_event_id.scope(),                \
                trace_event_id.raw_id(), thread_id, timestamp, &trace_event_args, trace_event_flags);                                                           \
        }                                                                                                                                                      \
    } while (0)
#define INTERNAL_TRACE_EVENT_METADATA_ADD(category_group, name, ...)                                                                                            \
    do {                                                                                                                                                       \
        const unsigned char* trace_event_category_group_enabled = trace_event_internal::GetCategoryGroupEnabled(category_group);                                \
        if (trace_event_category_group_enabled && *trace_event_category_group_enabled) {                                                                        \
            base::trace_event::TraceArguments trace_event_args { __VA_ARGS__ };                                                                                 \
            trace_event_internal::AddMetadataEvent(trace_event_category_group_enabled, name, &trace_event_args, TRACE_EVENT_FLAG_NONE);                         \
        }                                                                                                                                                      \
    } while (0)
#define INTERNAL_TRACE_EVENT_SCOPED(category_group, name, ...)                                                                                                 \
    INTERNAL_TRACE_TYPED_SCOPED_DISPATCH(category_group, name, ##__VA_ARGS__)
#define INTERNAL_TRACE_TYPED_SCOPED_DISPATCH(category_group, name, ...)                                                                                        \
    INTERNAL_TRACE_TYPED_SCOPED_PICK(_, ##__VA_ARGS__, INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE,                                 \
        INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE,        \
        INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE,        \
        INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE,        \
        INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_RECORD2, INTERNAL_TRACE_TYPED_SCOPED_IGNORE,       \
        INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_IGNORE, INTERNAL_TRACE_TYPED_SCOPED_0)(category_group, name, ##__VA_ARGS__)
#define INTERNAL_TRACE_TYPED_SCOPED_PICK(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, NAME, ...) NAME
#define INTERNAL_TRACE_TYPED_SCOPED_0(category_group, name)                                                                                                    \
    trace_event_internal::ScopedTraceEvent INTERNAL_TRACE_EVENT_UID(trace_event_scope_, __LINE__)(category_group, name)
#define INTERNAL_TRACE_TYPED_SCOPED_IGNORE(category_group, name, ...)                                                                                          \
    trace_event_internal::ScopedTraceEvent INTERNAL_TRACE_EVENT_UID(trace_event_scope_, __LINE__)(category_group, name)
#define INTERNAL_TRACE_TYPED_SCOPED_RECORD2(category_group, name, arg1_name, arg1_value, arg2_name, arg2_value)                                                \
    trace_event_internal::ScopedTraceEvent INTERNAL_TRACE_EVENT_UID(trace_event_scope_, __LINE__)(category_group, name, arg1_name, arg1_value, arg2_name,      \
        arg2_value)
#define INTERNAL_TRACE_TYPED_EVENT_DISPATCH(phase, category, name, flags, ...)                                                                                 \
    INTERNAL_TRACE_TYPED_EVENT_PICK(_, ##__VA_ARGS__, INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE,                                    \
        INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE,             \
        INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE,             \
        INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE,             \
        INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_RECORD2, INTERNAL_TRACE_TYPED_EVENT_IGNORE,            \
        INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_IGNORE, INTERNAL_TRACE_TYPED_EVENT_0)(phase, category, name, flags, ##__VA_ARGS__)
#define INTERNAL_TRACE_TYPED_EVENT_PICK(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, NAME, ...) NAME
#define INTERNAL_TRACE_TYPED_EVENT_0(phase, category, name, flags) trace_event_internal::AddTypedTraceEvent(phase, category, name, flags)
#define INTERNAL_TRACE_TYPED_EVENT_IGNORE(phase, category, name, flags, ...) trace_event_internal::AddTypedTraceEvent(phase, category, name, flags)
#define INTERNAL_TRACE_TYPED_EVENT_RECORD2(phase, category, name, flags, arg1_name, arg1_value, arg2_name, arg2_value)                                        \
    trace_event_internal::AddTypedTraceEvent(phase, category, name, flags, arg1_name, arg1_value, arg2_name, arg2_value)

// Defined in application_state_proto_android.h
#define TRACE_APPLICATION_STATE(...) INTERNAL_TRACE_IGNORE(__VA_ARGS__)

#ifndef TRACE_HEAP_PROFILER_API_SCOPED_TASK_EXECUTION
#define TRACE_HEAP_PROFILER_API_SCOPED_TASK_EXECUTION trace_event_internal::IgnoredValue
#endif

#define TRACE_ID_MANGLE(val) (val)

#define TRACE_EVENT_API_CURRENT_THREAD_ID 0

// Legacy trace macros
#define TRACE_EVENT0(category_group, name) INTERNAL_TRACE_EVENT_SCOPED(category_group, name)
#define TRACE_EVENT_WITH_FLOW0(category_group, name, bind_id, flags)                                                                                           \
    INTERNAL_TRACE_EVENT_ADD_WITH_BIND_ID(TRACE_EVENT_PHASE_COMPLETE, category_group, name, bind_id, flags)
#define TRACE_EVENT1(category_group, name, arg1_name, arg1_value) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COMPLETE, category_group, name, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_WITH_FLOW1(category_group, name, bind_id, flags, arg1_name, arg1_value)                                                                    \
    INTERNAL_TRACE_EVENT_ADD_WITH_BIND_ID(TRACE_EVENT_PHASE_COMPLETE, category_group, name, bind_id, flags, arg1_name, arg1_value)
#define TRACE_EVENT2(category_group, name, arg1_name, arg1_value, arg2_name, arg2_value)                                                                         \
    INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COMPLETE, category_group, name, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_WITH_FLOW2(category_group, name, bind_id, flags, arg1_name, arg1_value, arg2_name, arg2_value)                                              \
    INTERNAL_TRACE_EVENT_ADD_WITH_BIND_ID(TRACE_EVENT_PHASE_COMPLETE, category_group, name, bind_id, flags, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_INSTANT0(category_group, name, scope) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name, scope)
#define TRACE_EVENT_INSTANT1(category_group, name, scope, arg1_name, arg1_value) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name, scope, arg1_name, arg1_value)
#define TRACE_EVENT_INSTANT2(category_group, name, scope, arg1_name, arg1_value, arg2_name, arg2_value)                                                          \
    INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name, scope, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_COPY_INSTANT0(category_group, name, scope) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_COPY_INSTANT1(category_group, name, scope, ...) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_COPY_INSTANT2(category_group, name, scope, ...) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_INSTANT_WITH_FLAGS0(category_group, name, scope, flags) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name, (scope) | (flags))
#define TRACE_EVENT_INSTANT_WITH_FLAGS1(category_group, name, scope, flags, ...) TRACE_EVENT_INSTANT_WITH_FLAGS0(category_group, name, scope, flags)
#define TRACE_EVENT_INSTANT_WITH_TIMESTAMP0(category_group, name, scope, timestamp) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_INSTANT_WITH_TIMESTAMP1(category_group, name, scope, timestamp, ...) TRACE_EVENT_INSTANT0(category_group, name, scope)
#define TRACE_EVENT_BEGIN0(category_group, name) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_BEGIN1(category_group, name, arg1_name, arg1_value) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_BEGIN2(category_group, name, arg1_name, arg1_value, arg2_name, arg2_value)                                                                   \
    INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_BEGIN_WITH_FLAGS0(category_group, name, flags) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, flags)
#define TRACE_EVENT_BEGIN_WITH_FLAGS1(category_group, name, flags, arg1_name, arg1_value) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, flags, arg1_name, arg1_value)
#define TRACE_EVENT_COPY_BEGIN2(category_group, name, arg1_name, arg1_value, arg2_name, arg2_value)                                                            \
    INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_BEGIN_WITH_ID_TID_AND_TIMESTAMP0(category_group, name, id, thread_id, timestamp)                                                          \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, thread_id, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_COPY_BEGIN_WITH_ID_TID_AND_TIMESTAMP0(category_group, name, id, thread_id, timestamp)                                                     \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, thread_id, timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_BEGIN_WITH_ID_TID_AND_TIMESTAMP1(category_group, name, id, thread_id, timestamp, arg1_name, arg1_value)                              \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, thread_id, timestamp, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value)
#define TRACE_EVENT_COPY_BEGIN_WITH_ID_TID_AND_TIMESTAMP2(category_group, name, id, thread_id, timestamp, arg1_name, arg1_value, arg2_name, arg2_value)       \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, thread_id, timestamp, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_END0(category_group, name) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_END1(category_group, name, arg1_name, arg1_value) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_END2(category_group, name, arg1_name, arg1_value, arg2_name, arg2_value)                                                                     \
    INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_END_WITH_FLAGS0(category_group, name, flags) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, flags)
#define TRACE_EVENT_END_WITH_FLAGS1(category_group, name, flags, arg1_name, arg1_value) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, flags, arg1_name, arg1_value)
#define TRACE_EVENT_COPY_END2(category_group, name, arg1_name, arg1_value, arg2_name, arg2_value)                                                              \
    INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_MARK_WITH_TIMESTAMP0(category_group, name, timestamp)                                                                                     \
    INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(TRACE_EVENT_PHASE_MARK, category_group, name, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_MARK_WITH_TIMESTAMP1(category_group, name, timestamp, arg1_name, arg1_value)                                                              \
    INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(TRACE_EVENT_PHASE_MARK, category_group, name, timestamp, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_MARK_WITH_TIMESTAMP2(category_group, name, timestamp, arg1_name, arg1_value, arg2_name, arg2_value)                                       \
    INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(                                                                                                                  \
        TRACE_EVENT_PHASE_MARK, category_group, name, timestamp, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_COPY_MARK(category_group, name) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_MARK, category_group, name, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_MARK1(category_group, name, arg1_name, arg1_value)                                                                                   \
    INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_MARK, category_group, name, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value)
#define TRACE_EVENT_COPY_MARK_WITH_TIMESTAMP(category_group, name, timestamp)                                                                                 \
    INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(TRACE_EVENT_PHASE_MARK, category_group, name, timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_END_WITH_ID_TID_AND_TIMESTAMP0(category_group, name, id, thread_id, timestamp)                                                            \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, thread_id, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_COPY_END_WITH_ID_TID_AND_TIMESTAMP0(category_group, name, id, thread_id, timestamp)                                                       \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, thread_id, timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_END_WITH_ID_TID_AND_TIMESTAMP1(category_group, name, id, thread_id, timestamp, arg1_name, arg1_value)                                \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, thread_id, timestamp, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value)
#define TRACE_EVENT_COPY_END_WITH_ID_TID_AND_TIMESTAMP2(category_group, name, id, thread_id, timestamp, arg1_name, arg1_value, arg2_name, arg2_value)         \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, thread_id, timestamp, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_COUNTER1(category_group, name, value) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, TRACE_EVENT_FLAG_NONE, "value", value)
#define TRACE_COUNTER_WITH_FLAG1(category_group, name, value, flags) INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, flags, "value", value)
#define TRACE_COPY_COUNTER1(category_group, name, value) TRACE_COUNTER1(category_group, name, value)
#define TRACE_COUNTER2(category_group, name, arg1_name, arg1_value, arg2_name, arg2_value)                                                                       \
    INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_COPY_COUNTER2(category_group, name, arg1_name, arg1_value, arg2_name, arg2_value) TRACE_COUNTER2(category_group, name, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_COUNTER_WITH_TIMESTAMP1(category_group, name, value, timestamp) TRACE_COUNTER1(category_group, name, value)
#define TRACE_COUNTER_WITH_TIMESTAMP2(category_group, name, timestamp, arg1_name, arg1_value, arg2_name, arg2_value) TRACE_COUNTER2(category_group, name, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_COUNTER_ID1(category_group, name, id, value)                                                                                                    \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_COUNTER, category_group, name, id, TRACE_EVENT_FLAG_NONE, "value", value)
#define TRACE_COPY_COUNTER_ID1(category_group, name, id, value)                                                                                                \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_COUNTER, category_group, name, id, TRACE_EVENT_FLAG_COPY, "value", value)
#define TRACE_COUNTER_ID2(category_group, name, id, value1_name, value1_value, value2_name, value2_value)                                                     \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_COUNTER, category_group, name, id, TRACE_EVENT_FLAG_NONE,                                             \
        value1_name, value1_value, value2_name, value2_value)
#define TRACE_COPY_COUNTER_ID2(category_group, name, id, value1_name, value1_value, value2_name, value2_value)                                                \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_COUNTER, category_group, name, id, TRACE_EVENT_FLAG_COPY,                                             \
        value1_name, value1_value, value2_name, value2_value)
#define TRACE_EVENT_SAMPLE_WITH_ID1(category_group, name, id, arg1_name, arg1_value)                                                                          \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_SAMPLE, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_ASYNC_BEGIN0(category_group, name, id)                                                                                                    \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_ASYNC_BEGIN1(category_group, name, id, arg1_name, arg1_value)                                                                             \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_ASYNC_BEGIN2(category_group, name, id, arg1_name, arg1_value, arg2_name, arg2_value)                                                      \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_COPY_ASYNC_BEGIN0(category_group, name, id)                                                                                               \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_ASYNC_BEGIN1(category_group, name, id, arg1_name, arg1_value)                                                                        \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value)
#define TRACE_EVENT_COPY_ASYNC_BEGIN2(category_group, name, id, arg1_name, arg1_value, arg2_name, arg2_value)                                                 \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP0(category_group, name, id, timestamp)                                                                          \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP1(category_group, name, id, timestamp, arg1_name, arg1_value)                                                   \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP2(category_group, name, id, timestamp, arg1_name, arg1_value, arg2_name, arg2_value)                            \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, \
        TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_COPY_ASYNC_BEGIN_WITH_TIMESTAMP0(category_group, name, id, timestamp)                                                                     \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_ASYNC_STEP_INTO0(category_group, name, id, step)                                                                                          \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_STEP_INTO, category_group, name, id, TRACE_EVENT_FLAG_NONE, "step", step)
#define TRACE_EVENT_ASYNC_STEP_INTO1(category_group, name, id, step, arg1_name, arg1_value)                                                                   \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_STEP_INTO, category_group, name, id, TRACE_EVENT_FLAG_NONE, "step", step, arg1_name, arg1_value)
#define TRACE_EVENT_ASYNC_STEP_INTO_WITH_TIMESTAMP0(category_group, name, id, step, timestamp)                                                                \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_STEP_INTO, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE, "step", step)
#define TRACE_EVENT_ASYNC_STEP_PAST0(category_group, name, id, step)                                                                                          \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_STEP_PAST, category_group, name, id, TRACE_EVENT_FLAG_NONE, "step", step)
#define TRACE_EVENT_ASYNC_STEP_PAST1(category_group, name, id, step, arg1_name, arg1_value)                                                                   \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_STEP_PAST, category_group, name, id, TRACE_EVENT_FLAG_NONE, "step", step, arg1_name, arg1_value)
#define TRACE_EVENT_ASYNC_END0(category_group, name, id)                                                                                                      \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_ASYNC_END1(category_group, name, id, arg1_name, arg1_value)                                                                               \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_ASYNC_END2(category_group, name, id, arg1_name, arg1_value, arg2_name, arg2_value)                                                        \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_COPY_ASYNC_END0(category_group, name, id)                                                                                                 \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_ASYNC_END1(category_group, name, id, arg1_name, arg1_value)                                                                          \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value)
#define TRACE_EVENT_COPY_ASYNC_END2(category_group, name, id, arg1_name, arg1_value, arg2_name, arg2_value)                                                   \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP0(category_group, name, id, timestamp)                                                                            \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP1(category_group, name, id, timestamp, arg1_name, arg1_value)                                                     \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP2(category_group, name, id, timestamp, arg1_name, arg1_value, arg2_name, arg2_value)                              \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp,   \
        TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_COPY_ASYNC_END_WITH_TIMESTAMP0(category_group, name, id, timestamp)                                                                       \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN0(category_group, name, id)                                                                                           \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(category_group, name, id, arg1_name, arg1_value)                                                                    \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(category_group, name, id, arg1_name, arg1_value, arg2_name, arg2_value)                                             \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                                                                                                         \
        TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_FLAGS0(category_group, name, id, flags)                                                                         \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, flags)
#define TRACE_EVENT_NESTABLE_ASYNC_END0(category_group, name, id)                                                                                             \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_NESTABLE_ASYNC_END1(category_group, name, id, arg1_name, arg1_value)                                                                      \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_NESTABLE_ASYNC_END2(category_group, name, id, arg1_name, arg1_value, arg2_name, arg2_value)                                               \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                                                                                                         \
        TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_FLAGS0(category_group, name, id, flags)                                                                           \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, flags)
#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT0(category_group, name, id)                                                                                         \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT, category_group, name, id, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT1(category_group, name, id, arg1_name, arg1_value)                                                                  \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT2(category_group, name, id, arg1_name, arg1_value, arg2_name, arg2_value)                                           \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                                                                                                         \
        TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT, category_group, name, id, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN_WITH_TTS2(category_group, name, id, arg1_name, arg1_value, arg2_name, arg2_value)                               \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_ASYNC_TTS | TRACE_EVENT_FLAG_COPY,    \
        arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END_WITH_TTS2(category_group, name, id, arg1_name, arg1_value, arg2_name, arg2_value)                                 \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_ASYNC_TTS | TRACE_EVENT_FLAG_COPY,      \
        arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP0(category_group, name, id, timestamp)                                                                 \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP1(category_group, name, id, timestamp, arg1_name, arg1_value)                                          \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id,                                      \
        TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP_AND_FLAGS0(category_group, name, id, timestamp, flags)                                                \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, flags)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP0(category_group, name, id, timestamp)                                                                   \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP1(category_group, name, id, timestamp, arg1_name, arg1_value)                                            \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID,     \
        timestamp, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP2(category_group, name, id, timestamp, arg1_name, arg1_value, arg2_name, arg2_value)                     \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID,     \
        timestamp, TRACE_EVENT_FLAG_NONE, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP_AND_FLAGS0(category_group, name, id, timestamp, flags)                                                  \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, flags)
#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT_WITH_TIMESTAMP0(category_group, name, id, timestamp)                                                               \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN0(category_group, name, id)                                                                                      \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN1(category_group, name, id, arg1_name, arg1_value)                                                               \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN2(category_group, name, id, arg1_name, arg1_value, arg2_name, arg2_value)                                        \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value, arg2_name, arg2_value)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END0(category_group, name, id)                                                                                        \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP0(category_group, name, id, timestamp)                                                            \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP1(category_group, name, id, timestamp, arg1_name, arg1_value)                                     \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID,   \
        timestamp, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END_WITH_TIMESTAMP0(category_group, name, id, timestamp)                                                              \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                                                                                                       \
        TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END1(category_group, name, id, arg1_name, arg1_value)                                                                 \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_value)
#define TRACE_EVENT_METADATA1(category_group, name, arg1_name, arg1_value)                                                                                    \
    INTERNAL_TRACE_EVENT_METADATA_ADD(category_group, name, arg1_name, arg1_value)
#define TRACE_EVENT_CLOCK_SYNC_RECEIVER(sync_id)                                                                                                              \
    INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_CLOCK_SYNC, "__metadata", "clock_sync", TRACE_EVENT_FLAG_NONE, "sync_id", sync_id)
#define TRACE_EVENT_CLOCK_SYNC_ISSUER(sync_id, issue_ts, issue_end_ts)                                                                                        \
    INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(TRACE_EVENT_PHASE_CLOCK_SYNC, "__metadata", "clock_sync", issue_end_ts, TRACE_EVENT_FLAG_NONE, "sync_id", sync_id, "issue_ts", issue_ts)
#define TRACE_EVENT_OBJECT_CREATED_WITH_ID(category_group, name, id)                                                                                          \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_CREATE_OBJECT, category_group, name, id, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(category_group, name, id, snapshot)                                                                                \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_SNAPSHOT_OBJECT, category_group, name, id, TRACE_EVENT_FLAG_NONE, "snapshot", snapshot)
#define TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID_AND_TIMESTAMP(category_group, name, id, timestamp, snapshot)                                                       \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(TRACE_EVENT_PHASE_SNAPSHOT_OBJECT, category_group, name, id, TRACE_EVENT_API_CURRENT_THREAD_ID,        \
        timestamp, TRACE_EVENT_FLAG_NONE, "snapshot", snapshot)
#define TRACE_EVENT_OBJECT_DELETED_WITH_ID(category_group, name, id)                                                                                          \
    INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_DELETE_OBJECT, category_group, name, id, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_CATEGORY_GROUP_ENABLED(category_group, ret)                                                                                                \
    do {                                                                                                                                                       \
        *ret = trace_event_internal::CategoryGroupEnabled(category_group);                                                                                      \
    } while (0)
#define TRACE_EVENT_IS_NEW_TRACE(ret)                                                                                                                          \
    do {                                                                                                                                                       \
        *ret = trace_event_internal::GetNumTracesRecorded() > 0;                                                                                               \
    } while (0)

// Typed macros. The lightweight backend records common "name", value debug
// annotations and intentionally ignores Perfetto-only tracks/lambdas.
#define TRACE_EVENT_BEGIN(category, name, ...) INTERNAL_TRACE_TYPED_EVENT_DISPATCH(TRACE_EVENT_PHASE_BEGIN, category, name, TRACE_EVENT_FLAG_NONE, ##__VA_ARGS__)
#define TRACE_EVENT_END(category, ...) INTERNAL_TRACE_TYPED_EVENT_DISPATCH(TRACE_EVENT_PHASE_END, category, "TRACE_EVENT_END", TRACE_EVENT_FLAG_NONE, ##__VA_ARGS__)
#define TRACE_EVENT(category, name, ...) INTERNAL_TRACE_EVENT_SCOPED(category, name, ##__VA_ARGS__)
#define TRACE_EVENT_INSTANT(category, name, ...)                                                                                                               \
    INTERNAL_TRACE_TYPED_EVENT_DISPATCH(TRACE_EVENT_PHASE_INSTANT, category, name, TRACE_EVENT_SCOPE_THREAD | TRACE_EVENT_FLAG_COPY, ##__VA_ARGS__)

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

class TraceEventMemoryOverhead;

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
    virtual void EstimateTraceMemoryOverhead(TraceEventMemoryOverhead* overhead);
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

    explicit TracedValue(size_t capacity = 0);

    static DictionaryWriter Dictionary(std::initializer_list<DictionaryWriter::Entry> entries)
    {
        return DictionaryWriter(entries);
    }

    void EndDictionary();
    void EndArray();

    void SetInteger(const char* name, int value);
    void SetDouble(const char* name, double value);
    void SetBoolean(const char* name, bool value);
    void SetString(const char* name, std::string_view value);
    void SetValue(const char* name, TracedValue* value);
    void SetPointer(const char* name, const void* value);
    void BeginDictionary(const char* name);
    void BeginArray(const char* name);

    void SetIntegerWithCopiedName(std::string_view name, int value);
    void SetDoubleWithCopiedName(std::string_view name, double value);
    void SetBooleanWithCopiedName(std::string_view name, bool value);
    void SetStringWithCopiedName(std::string_view name, std::string_view value);
    void SetValueWithCopiedName(std::string_view name, TracedValue* value);
    void SetPointerWithCopiedName(std::string_view name, const void* value);
    void BeginDictionaryWithCopiedName(std::string_view name);
    void BeginArrayWithCopiedName(std::string_view name);

    void AppendInteger(int value);
    void AppendDouble(double value);
    void AppendBoolean(bool value);
    void AppendString(std::string_view value);
    void AppendPointer(const void* value);
    void BeginArray();
    void BeginDictionary();

    void AppendAsTraceFormat(std::string* out) const override;

private:
    enum class ContainerKind {
        kDictionary,
        kArray,
    };

    struct ContainerState {
        ContainerKind kind;
        bool has_value = false;
    };

    static constexpr size_t kMaxContainerDepth = 32;

    ContainerState* CurrentContainer();
    const ContainerState* CurrentContainer() const;
    bool PushContainer(ContainerKind kind);
    void PopContainer();

    void AppendName(std::string_view name);
    void AppendValuePrefix();
    void AppendRawValue(std::string_view value);
    std::string ToTraceFormatString() const;

    std::string json_;
    std::array<ContainerState, kMaxContainerDepth> stack_ {};
    size_t stack_size_ = 0;
};

class BASE_EXPORT TracedValueJSON : public TracedValue {
public:
    explicit TracedValueJSON(size_t capacity = 0)
        : TracedValue(capacity)
    {
    }

    std::unique_ptr<base::Value> ToBaseValue() const;
    std::string ToJSON() const;
    std::string ToFormattedJSON() const;
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
        TraceValue ret {};
        ret.Init(std::forward<T>(value));
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

public:
    template <typename T, class = void>
    struct Helper {
        static constexpr unsigned char kType = TRACE_VALUE_TYPE_COPY_STRING;
        static inline void SetValue(TraceValue* v, const T&)
        {
            v->as_string = "";
        }
    };

    template <typename T, class = void>
    struct TypeFor {
        static constexpr unsigned char value = Helper<std::decay_t<T>>::kType;
    };

    template <class T> void Init(T&& value)
    {
        using ValueType = std::decay_t<T>;
        Helper<ValueType>::SetValue(this, std::forward<T>(value));
    }
};

template <typename T>
struct TraceValue::Helper<T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>>> {
    static constexpr unsigned char kType = std::is_signed_v<T> ? TRACE_VALUE_TYPE_INT : TRACE_VALUE_TYPE_UINT;
    static inline void SetValue(TraceValue* v, T value)
    {
        if constexpr (std::is_signed_v<T>)
            v->as_int = static_cast<long long>(value);
        else
            v->as_uint = static_cast<unsigned long long>(value);
    }
};

template <typename T>
struct TraceValue::Helper<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_DOUBLE;
    static inline void SetValue(TraceValue* v, T value) { v->as_double = static_cast<double>(value); }
};

template <>
struct TraceValue::Helper<bool> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_BOOL;
    static inline void SetValue(TraceValue* v, bool value) { v->as_bool = value; }
};

template <>
struct TraceValue::Helper<const void*> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_POINTER;
    static inline void SetValue(TraceValue* v, const void* value) { v->as_pointer = value; }
};

template <>
struct TraceValue::Helper<void*> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_POINTER;
    static inline void SetValue(TraceValue* v, void* value) { v->as_pointer = value; }
};

template <typename T>
struct TraceValue::Helper<T*, std::enable_if_t<!std::is_same_v<T, char> && !std::is_same_v<T, const char>>> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_POINTER;
    static inline void SetValue(TraceValue* v, T* value) { v->as_pointer = value; }
};

template <>
struct TraceValue::Helper<const char*> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_STRING;
    static inline void SetValue(TraceValue* v, const char* value) { v->as_string = value; }
};

template <>
struct TraceValue::Helper<char*> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_STRING;
    static inline void SetValue(TraceValue* v, char* value) { v->as_string = value; }
};

template <>
struct TraceValue::Helper<std::string> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_COPY_STRING;
    static inline void SetValue(TraceValue* v, const std::string& value) { v->as_string = value.c_str(); }
};

template <>
struct TraceValue::Helper<std::string_view> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_COPY_STRING;
    static inline void SetValue(TraceValue* v, std::string_view value)
    {
        thread_local std::string storage;
        storage.assign(value.data(), value.size());
        v->as_string = storage.c_str();
    }
};

template <typename CONVERTABLE_TYPE>
struct TraceValue::Helper<std::unique_ptr<CONVERTABLE_TYPE>, std::enable_if_t<std::is_convertible_v<CONVERTABLE_TYPE*, ConvertableToTraceFormat*>>> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_CONVERTABLE;
    static inline void SetValue(TraceValue* v, std::unique_ptr<CONVERTABLE_TYPE> value) { v->as_convertable = value.release(); }
};

class TraceStringWithCopy {
public:
    explicit TraceStringWithCopy(const char* str)
        : str_(str)
    {
    }
    const char* str() const { return str_; }

private:
    const char* str_;
};

template <>
struct TraceValue::Helper<TraceStringWithCopy> {
    static constexpr unsigned char kType = TRACE_VALUE_TYPE_COPY_STRING;
    static inline void SetValue(TraceValue* v, const TraceStringWithCopy& value) { v->as_string = value.str(); }
};

class TraceArguments;

class BASE_EXPORT StringStorage {
public:
    constexpr StringStorage() = default;
    explicit StringStorage(size_t alloc_size) { Reset(alloc_size); }
    ~StringStorage();

    StringStorage(const StringStorage&) = delete;
    StringStorage& operator=(const StringStorage&) = delete;
    StringStorage(StringStorage&& other) noexcept;
    StringStorage& operator=(StringStorage&& other) noexcept;

    void Reset(size_t alloc_size = 0);

    constexpr size_t size() const { return data_ ? data_->size : 0u; }
    constexpr const char* data() const { return data_ ? data_->chars : nullptr; }
    constexpr char* data() { return data_ ? data_->chars : nullptr; }
    constexpr const char* begin() const { return data(); }
    constexpr const char* end() const { return data() + size(); }
    constexpr bool empty() const { return size() == 0; }
    constexpr bool Contains(const void* ptr) const
    {
        const char* char_ptr = static_cast<const char*>(ptr);
        return char_ptr >= begin() && char_ptr < end();
    }
    bool Contains(const TraceArguments& args) const;
    constexpr size_t EstimateTraceMemoryOverhead() const
    {
        return data_ ? sizeof(size_t) + data_->size : 0u;
    }

private:
    struct Data {
        size_t size = 0;
        char chars[1];
    };

    RAW_PTR_EXCLUSION Data* data_ = nullptr;
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
        types_[0] = TraceValue::TypeFor<T>::value;
        names_[0] = arg1_name;
        values_[0].Init(std::forward<T>(arg1_value));
    }

    // Constructor for two arguments.
    template <typename T1, typename T2
        /*, class = decltype(TraceValue::TypeCheck<T1>::value&& TraceValue::TypeCheck<T2>::value)*/>
    TraceArguments(const char* arg1_name, T1&& arg1_value, const char* arg2_name, T2&& arg2_value)
        : size_(2)
    {
        types_[0] = TraceValue::TypeFor<T1>::value;
        types_[1] = TraceValue::TypeFor<T2>::value;
        names_[0] = arg1_name;
        names_[1] = arg2_name;
        values_[0].Init(std::forward<T1>(arg1_value));
        values_[1].Init(std::forward<T2>(arg2_value));
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
        if (num_args > static_cast<int>(kMaxSize))
            num_args = static_cast<int>(kMaxSize);
        size_ = static_cast<unsigned char>(std::max(num_args, 0));
        for (size_t n = 0; n < size_; ++n) {
            types_[n] = arg_types ? arg_types[n] : TRACE_VALUE_TYPE_UINT;
            names_[n] = arg_names ? arg_names[n] : nullptr;
            if (types_[n] == TRACE_VALUE_TYPE_CONVERTABLE)
                values_[n].Init(std::forward<CONVERTABLE_TYPE>(std::move(arg_convertables[n])));
            else
                values_[n].as_uint = arg_values ? arg_values[n] : 0;
        }
    }

    // Destructor. NOTE: Intentionally inlined (see note above).
    ~TraceArguments()
    {
        for (size_t n = 0; n < size_; ++n) {
            if (types_[n] == TRACE_VALUE_TYPE_CONVERTABLE)
                delete values_[n].as_convertable;
        }
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
    const TraceValue* values() const { return values_; }

    // Reset to empty arguments list.
    void Reset();

    void CopyStringsTo(StringStorage* storage,
        bool copy_all_strings,
        const char** extra_string1,
        const char** extra_string2);

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
    TraceValue values_[kMaxSize];
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
    template <typename Category, typename Name, typename... Args>
    ScopedTraceEvent(const Category& category_group, const Name& name, Args&&... args);
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

// Lightweight implementation for
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

template <size_t N>
inline const char* TraceCStringOrNull(const char (&value)[N])
{
    return value;
}

template <size_t N>
inline const char* TraceCStringOrNull(char (&value)[N])
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

template <typename T>
struct IsTypedTraceArgName : std::false_type {
};

template <>
struct IsTypedTraceArgName<const char*> : std::true_type {
};

template <>
struct IsTypedTraceArgName<char*> : std::true_type {
};

template <size_t N>
struct IsTypedTraceArgName<const char (&)[N]> : std::true_type {
};

template <size_t N>
struct IsTypedTraceArgName<char (&)[N]> : std::true_type {
};

template <size_t N>
struct IsTypedTraceArgName<char[N]> : std::true_type {
};

template <size_t N>
struct IsTypedTraceArgName<const char[N]> : std::true_type {
};

template <>
struct IsTypedTraceArgName<perfetto::StaticString> : std::true_type {
};

template <>
struct IsTypedTraceArgName<perfetto::DynamicString> : std::true_type {
};

template <typename T>
inline constexpr bool IsTypedTraceArgNameV = IsTypedTraceArgName<std::remove_cv_t<std::remove_reference_t<T>>>::value;

template <typename T>
const char* TraceArgNameCString(const T& value)
{
    return TraceCStringOrNull(value);
}

template <typename Arg1Name, typename Arg1Value, typename Arg2Name, typename Arg2Value, typename... Rest>
base::trace_event::TraceEventHandle AddTypedTraceEventWithTwoArgs(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    unsigned int flags,
    Arg1Name&& arg1_name,
    Arg1Value&& arg1_value,
    Arg2Name&& arg2_name,
    Arg2Value&& arg2_value,
    Rest&&... rest);

inline base::trace_event::TraceEventHandle AddTypedTraceEventWithArgs(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    unsigned int flags)
{
    base::trace_event::TraceArguments args;
    return AddTraceEvent(phase, category_group_enabled, name, nullptr, 0, &args, flags | TRACE_EVENT_FLAG_COPY);
}

template <typename Arg>
base::trace_event::TraceEventHandle AddTypedTraceEventWithArgs(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    unsigned int flags,
    Arg&& arg)
{
    INTERNAL_TRACE_IGNORE(std::forward<Arg>(arg));
    return AddTypedTraceEventWithArgs(phase, category_group_enabled, name, flags);
}

template <typename ArgName, typename ArgValue, typename... Rest>
base::trace_event::TraceEventHandle AddTypedTraceEventWithArgs(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    unsigned int flags,
    ArgName&& arg_name,
    ArgValue&& arg_value,
    Rest&&... rest)
{
    if constexpr (IsTypedTraceArgNameV<ArgName>) {
        if constexpr (sizeof...(Rest) >= 2) {
            return AddTypedTraceEventWithTwoArgs(phase, category_group_enabled, name, flags,
                std::forward<ArgName>(arg_name), std::forward<ArgValue>(arg_value),
                std::forward<Rest>(rest)...);
        } else {
            INTERNAL_TRACE_IGNORE(std::forward<Rest>(rest)...);
            base::trace_event::TraceArguments args(TraceArgNameCString(arg_name), std::forward<ArgValue>(arg_value));
            return AddTraceEvent(phase, category_group_enabled, name, nullptr, 0, &args, flags | TRACE_EVENT_FLAG_COPY);
        }
    } else {
        INTERNAL_TRACE_IGNORE(std::forward<ArgName>(arg_name), std::forward<ArgValue>(arg_value), std::forward<Rest>(rest)...);
        return AddTypedTraceEventWithArgs(phase, category_group_enabled, name, flags);
    }
}

template <typename Arg1Name, typename Arg1Value, typename Arg2Name, typename Arg2Value, typename... Rest>
base::trace_event::TraceEventHandle AddTypedTraceEventWithTwoArgs(char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    unsigned int flags,
    Arg1Name&& arg1_name,
    Arg1Value&& arg1_value,
    Arg2Name&& arg2_name,
    Arg2Value&& arg2_value,
    Rest&&... rest)
{
    if constexpr (IsTypedTraceArgNameV<Arg2Name>) {
        INTERNAL_TRACE_IGNORE(std::forward<Rest>(rest)...);
        base::trace_event::TraceArguments args(TraceArgNameCString(arg1_name), std::forward<Arg1Value>(arg1_value),
            TraceArgNameCString(arg2_name), std::forward<Arg2Value>(arg2_value));
        return AddTraceEvent(phase, category_group_enabled, name, nullptr, 0, &args, flags | TRACE_EVENT_FLAG_COPY);
    } else {
        INTERNAL_TRACE_IGNORE(std::forward<Arg2Name>(arg2_name), std::forward<Arg2Value>(arg2_value), std::forward<Rest>(rest)...);
        base::trace_event::TraceArguments args(TraceArgNameCString(arg1_name), std::forward<Arg1Value>(arg1_value));
        return AddTraceEvent(phase, category_group_enabled, name, nullptr, 0, &args, flags | TRACE_EVENT_FLAG_COPY);
    }
}

template <typename Category, typename Name>
base::trace_event::TraceEventHandle AddTypedTraceEvent(char phase, const Category& category_group, const Name& name, unsigned int flags)
{
    std::string category_storage = TraceStringCopy(category_group);
    std::string name_storage = TraceStringCopy(name);
    const unsigned char* category_group_enabled = GetCategoryGroupEnabled(category_storage.c_str());
    if (!category_group_enabled || !*category_group_enabled)
        return {};

    base::trace_event::TraceArguments args;
    return AddTraceEvent(phase, category_group_enabled, name_storage.c_str(), nullptr, 0, &args, flags | TRACE_EVENT_FLAG_COPY);
}

template <typename Category, typename Name, typename... Args>
base::trace_event::TraceEventHandle AddTypedTraceEvent(char phase, const Category& category_group, const Name& name, unsigned int flags, Args&&... args)
{
    std::string category_storage = TraceStringCopy(category_group);
    std::string name_storage = TraceStringCopy(name);
    const unsigned char* category_group_enabled = GetCategoryGroupEnabled(category_storage.c_str());
    if (!category_group_enabled || !*category_group_enabled)
        return {};

    return AddTypedTraceEventWithArgs(phase, category_group_enabled, name_storage.c_str(), flags, std::forward<Args>(args)...);
}

template <typename Category, typename Name>
ScopedTraceEvent::ScopedTraceEvent(const Category& category_group, const Name& name)
    : ScopedTraceEvent(category_group, name, trace_event_internal::IgnoredValue())
{
}

template <typename Category, typename Name, typename... Args>
ScopedTraceEvent::ScopedTraceEvent(const Category& category_group, const Name& name, Args&&... args)
{
    std::string category_storage = TraceStringCopy(category_group);
    name_storage_ = TraceStringCopy(name);
    category_group_enabled_ = GetCategoryGroupEnabled(category_storage.c_str());
    if (!category_group_enabled_ || !*category_group_enabled_)
        return;

    name_ = name_storage_.c_str();
    handle_ = AddTypedTraceEventWithArgs(TRACE_EVENT_PHASE_BEGIN, category_group_enabled_, name_,
        TRACE_EVENT_FLAG_COPY, std::forward<Args>(args)...);
}

} // namespace trace_event_internal

#endif // BASE_TRACE_EVENT_TRACE_EVENT_LIGHTWEIGHT_H_
