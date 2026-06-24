// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/ui_base_features.h"

#include "base/feature_list.h"
#include "build/build_config.h"

namespace features {

#if BUILDFLAG(IS_APPLE)
BASE_FEATURE(kCr2023MacFontSmoothing,
             "Cr2023MacFontSmoothing",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif

BASE_FEATURE(kSystemCaptionStyle,
             "SystemCaptionStyle",
             base::FEATURE_ENABLED_BY_DEFAULT);

}  // namespace features
