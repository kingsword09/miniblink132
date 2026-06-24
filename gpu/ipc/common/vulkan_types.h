// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_COMMON_VULKAN_TYPES_H_
#define GPU_IPC_COMMON_VULKAN_TYPES_H_

#if __has_include(<vulkan/vulkan_core.h>)
#include <vulkan/vulkan_core.h>
#else
#include "third_party/skia/include/third_party/vulkan/vulkan/vulkan_core.h"
#endif

#endif // GPU_IPC_COMMON_VULKAN_TYPES_H_
