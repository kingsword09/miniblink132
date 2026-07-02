#ifndef electron_common_TracingControllerImpl_h
#define electron_common_TracingControllerImpl_h

#include "v8/include/v8-platform.h"

#include <stdint.h>

#include <memory>
#include <vector>

namespace base {
class Lock;
}

namespace atom {

class TracingControllerImpl : public v8::TracingController {
public:
    TracingControllerImpl();
    ~TracingControllerImpl() override;

    TracingControllerImpl(const TracingControllerImpl&) = delete;
    TracingControllerImpl& operator=(const TracingControllerImpl&) = delete;

    const uint8_t* GetCategoryGroupEnabled(const char* name) override;
    uint64_t AddTraceEvent(char phase,
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
        unsigned int flags) override;
    uint64_t AddTraceEventWithTimestamp(char phase,
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
        int64_t timestamp_microseconds) override;
    void UpdateTraceEventDuration(const uint8_t* category_enabled_flag,
        const char* name,
        uint64_t handle) override;
    void AddTraceStateObserver(TraceStateObserver* observer) override;
    void RemoveTraceStateObserver(TraceStateObserver* observer) override;

private:
    class TraceLogObserver;

    std::vector<TraceStateObserver*> CopyObservers();
    void NotifyTraceEnabled();
    void NotifyTraceDisabled();

    std::unique_ptr<base::Lock> observers_lock_;
    std::vector<TraceStateObserver*> observers_;
    std::unique_ptr<TraceLogObserver> trace_log_observer_;
};

} // namespace atom

#endif
