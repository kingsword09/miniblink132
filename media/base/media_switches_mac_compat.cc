// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/media_switches.h"

#include "base/feature_list.h"

namespace media {

BASE_FEATURE(kMediaSharedBitmapToSharedImage,
             "MediaSharedBitmapToSharedImage",
             base::FEATURE_ENABLED_BY_DEFAULT);

}  // namespace media
