// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/common/gpu_memory_buffer_impl_shared_memory.h"

namespace gpu {

// The full shared-memory implementation depends on the GL binding stack. The
// mac lite build only needs the factory symbols so GpuMemoryBufferSupport can
// reject unsupported shared-memory buffers without pulling that dependency tree.
std::unique_ptr<GpuMemoryBufferImplSharedMemory>
GpuMemoryBufferImplSharedMemory::CreateFromHandle(gfx::GpuMemoryBufferHandle handle,
                                                  const gfx::Size& size,
                                                  gfx::BufferFormat format,
                                                  gfx::BufferUsage usage,
                                                  DestructionCallback callback)
{
    return nullptr;
}

bool GpuMemoryBufferImplSharedMemory::IsConfigurationSupported(gfx::BufferFormat format,
                                                               gfx::BufferUsage usage)
{
    return false;
}

}  // namespace gpu
