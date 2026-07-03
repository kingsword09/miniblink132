// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "ui/gfx/font_render_params.h"

#include <string>

typedef struct _FcConfig FcConfig;
typedef struct _FcPattern FcPattern;

namespace gfx {

FcConfig* GetGlobalFontConfig()
{
    return nullptr;
}

std::string GetFontName(FcPattern*)
{
    return std::string();
}

base::FilePath GetFontPath(FcPattern*)
{
    return base::FilePath();
}

int GetFontTtcIndex(FcPattern*)
{
    return 0;
}

bool IsFontBold(FcPattern*)
{
    return false;
}

bool IsFontItalic(FcPattern*)
{
    return false;
}

bool IsFontScalable(FcPattern*)
{
    return false;
}

std::string GetFontFormat(FcPattern*)
{
    return std::string();
}

void GetFontRenderParamsFromFcPattern(FcPattern*, FontRenderParams*)
{
}

}  // namespace gfx
