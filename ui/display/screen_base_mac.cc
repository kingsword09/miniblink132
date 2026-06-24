// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/display/screen_base.h"

#include "ui/display/display.h"

namespace display {

namespace {

class NativeScreenBase : public ScreenBase {
public:
    NativeScreenBase()
    {
        ProcessDisplayChanged(Display::GetDefaultDisplay(), true);
    }
};

} // namespace

Screen* CreateNativeScreen()
{
    return new NativeScreenBase();
}

} // namespace display
