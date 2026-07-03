// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "mbvip/core/mb.h"

namespace content {

uint32_t g_contextMenuItemMask =
    kMbMenuSelectedAllId | kMbMenuSelectedTextId | kMbMenuUndoId |
    kMbMenuCopyImageId | kMbMenuSaveImageId | kMbMenuInspectElementAtId |
    kMbMenuCutId | kMbMenuPasteId;

}  // namespace content
