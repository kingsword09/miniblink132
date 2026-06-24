// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_THREADING_THREAD_TASK_RUNNER_HANDLE_H_
#define BASE_THREADING_THREAD_TASK_RUNNER_HANDLE_H_

#include "base/task/single_thread_task_runner.h"

namespace base {

class BASE_EXPORT ThreadTaskRunnerHandle {
public:
    static const scoped_refptr<SingleThreadTaskRunner>& Get()
    {
        return SingleThreadTaskRunner::GetCurrentDefault();
    }

    static bool IsSet()
    {
        return SingleThreadTaskRunner::HasCurrentDefault();
    }
};

using ScopedThreadTaskRunnerHandle = SingleThreadTaskRunner::CurrentDefaultHandle;

} // namespace base

#endif // BASE_THREADING_THREAD_TASK_RUNNER_HANDLE_H_
