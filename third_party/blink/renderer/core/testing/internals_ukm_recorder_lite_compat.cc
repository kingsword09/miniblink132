// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/testing/internals_ukm_recorder.h"

namespace blink {

InternalsUkmRecorder::InternalsUkmRecorder(Document*)
{
}

HeapVector<ScriptValue> InternalsUkmRecorder::getMetrics(
    ScriptState*,
    const String&,
    const Vector<String>&)
{
    return {};
}

}  // namespace blink
