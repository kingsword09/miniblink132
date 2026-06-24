// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/platform_thread_internal_posix.h"

namespace base::internal {

const ThreadPriorityToNiceValuePairForTest kThreadPriorityToNiceValueMapForTest[7] = {
    {ThreadPriorityForTest::kRealtimeAudio, -10},
    {ThreadPriorityForTest::kDisplay, -8},
    {ThreadPriorityForTest::kNormal, 0},
    {ThreadPriorityForTest::kResourceEfficient, 1},
    {ThreadPriorityForTest::kUtility, 2},
    {ThreadPriorityForTest::kBackground, 10},
};

const ThreadTypeToNiceValuePair kThreadTypeToNiceValueMap[7] = {
    {ThreadType::kBackground, 10},
    {ThreadType::kUtility, 2},
    {ThreadType::kResourceEfficient, 1},
    {ThreadType::kDefault, 0},
    {ThreadType::kDisplayCritical, -8},
    {ThreadType::kRealtimeAudio, -10},
};

}  // namespace base::internal
