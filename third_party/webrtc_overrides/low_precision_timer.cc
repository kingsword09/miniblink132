// Copyright 2026 The miniblink Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/webrtc_overrides/low_precision_timer.h"

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/synchronization/lock.h"

namespace blink {

struct LowPrecisionTimer::State : public base::RefCountedThreadSafe<LowPrecisionTimer::State> {
    State(scoped_refptr<base::SequencedTaskRunner> task_runner, base::RepeatingClosure callback)
        : task_runner(std::move(task_runner))
        , callback(std::move(callback))
    {
    }

    mutable base::Lock lock;
    scoped_refptr<base::SequencedTaskRunner> task_runner;
    base::RepeatingClosure callback;
    base::TimeDelta delay;
    uint64_t generation = 0;
    bool active = false;
    bool repeating = false;
    bool shutdown = false;

private:
    friend class base::RefCountedThreadSafe<State>;
    ~State() = default;
};

LowPrecisionTimer::LowPrecisionTimer(scoped_refptr<base::SequencedTaskRunner> task_runner, base::RepeatingClosure callback)
    : state_(base::MakeRefCounted<State>(std::move(task_runner), std::move(callback)))
{
}

LowPrecisionTimer::~LowPrecisionTimer()
{
    Shutdown();
}

void LowPrecisionTimer::StartOneShot(base::TimeDelta delay)
{
    Start(delay, false);
}

void LowPrecisionTimer::StartRepeating(base::TimeDelta delay)
{
    Start(delay, true);
}

void LowPrecisionTimer::Start(base::TimeDelta delay, bool repeating)
{
    uint64_t generation;
    {
        base::AutoLock locker(state_->lock);
        if (state_->shutdown)
            return;
        state_->delay = delay;
        state_->active = true;
        state_->repeating = repeating;
        generation = ++state_->generation;
    }
    PostTask(state_, generation, delay);
}

void LowPrecisionTimer::MoveToNewTaskRunner(scoped_refptr<base::SequencedTaskRunner> task_runner)
{
    base::AutoLock locker(state_->lock);
    if (!state_->shutdown)
        state_->task_runner = std::move(task_runner);
}

void LowPrecisionTimer::Stop()
{
    base::AutoLock locker(state_->lock);
    state_->active = false;
    ++state_->generation;
}

void LowPrecisionTimer::Shutdown()
{
    base::AutoLock locker(state_->lock);
    state_->active = false;
    state_->shutdown = true;
    state_->callback.Reset();
    ++state_->generation;
}

bool LowPrecisionTimer::IsActive() const
{
    base::AutoLock locker(state_->lock);
    return state_->active;
}

void LowPrecisionTimer::PostTask(scoped_refptr<State> state, uint64_t generation, base::TimeDelta delay)
{
    scoped_refptr<base::SequencedTaskRunner> task_runner;
    {
        base::AutoLock locker(state->lock);
        if (state->shutdown || !state->active || state->generation != generation)
            return;
        task_runner = state->task_runner;
    }
    task_runner->PostDelayedTask(FROM_HERE, base::BindOnce(&LowPrecisionTimer::Run, std::move(state), generation), delay);
}

void LowPrecisionTimer::Run(scoped_refptr<State> state, uint64_t generation)
{
    scoped_refptr<base::SequencedTaskRunner> task_runner;
    base::RepeatingClosure callback;
    base::TimeDelta delay;
    bool repeating;
    {
        base::AutoLock locker(state->lock);
        if (state->shutdown || !state->active || state->generation != generation)
            return;
        task_runner = state->task_runner;
        if (!task_runner->RunsTasksInCurrentSequence()) {
            task_runner->PostTask(FROM_HERE, base::BindOnce(&LowPrecisionTimer::Run, std::move(state), generation));
            return;
        }

        repeating = state->repeating;
        delay = state->delay;
        callback = state->callback;
        if (!repeating)
            state->active = false;
    }

    if (callback)
        callback.Run();

    if (repeating)
        PostTask(std::move(state), generation, delay);
}

} // namespace blink
