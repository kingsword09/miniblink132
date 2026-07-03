// Copyright 2026 The miniblink Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_WEBRTC_OVERRIDES_LOW_PRECISION_TIMER_H_
#define THIRD_PARTY_WEBRTC_OVERRIDES_LOW_PRECISION_TIMER_H_

#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"

namespace blink {

// Minimal Chromium-compatible LowPrecisionTimer used by Blink media stream
// code. It keeps cancellation thread-safe by versioning posted tasks instead of
// relying on base::Timer's single-sequence lifetime rules.
class LowPrecisionTimer {
public:
    LowPrecisionTimer(scoped_refptr<base::SequencedTaskRunner> task_runner, base::RepeatingClosure callback);
    LowPrecisionTimer(const LowPrecisionTimer&) = delete;
    LowPrecisionTimer& operator=(const LowPrecisionTimer&) = delete;
    ~LowPrecisionTimer();

    void StartOneShot(base::TimeDelta delay);
    void StartRepeating(base::TimeDelta delay);
    void MoveToNewTaskRunner(scoped_refptr<base::SequencedTaskRunner> task_runner);
    void Stop();
    void Shutdown();
    bool IsActive() const;

private:
    struct State;

    void Start(base::TimeDelta delay, bool repeating);
    static void Run(scoped_refptr<State> state, uint64_t generation);
    static void PostTask(scoped_refptr<State> state, uint64_t generation, base::TimeDelta delay);

    scoped_refptr<State> state_;
};

} // namespace blink

#endif // THIRD_PARTY_WEBRTC_OVERRIDES_LOW_PRECISION_TIMER_H_
