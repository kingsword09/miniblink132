// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/breakout_box/media_stream_track_generator.h"

namespace blink {

MediaStreamTrackGenerator* MediaStreamTrackGenerator::Create(
    ScriptState*,
    const String&,
    ExceptionState&)
{
    return nullptr;
}

MediaStreamTrackGenerator* MediaStreamTrackGenerator::Create(
    ScriptState*,
    MediaStreamTrackGeneratorInit*,
    ExceptionState&)
{
    return nullptr;
}

WritableStream* MediaStreamTrackGenerator::writable(ScriptState*)
{
    return nullptr;
}

}  // namespace blink
