// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/c/system/platform_handle.h"

extern "C" {

MojoResult MojoWrapPlatformHandle(const MojoPlatformHandle* platform_handle,
                                  const MojoWrapPlatformHandleOptions* options,
                                  MojoHandle* mojo_handle)
{
    if (mojo_handle)
        *mojo_handle = MOJO_HANDLE_INVALID;
    return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult MojoUnwrapPlatformHandle(MojoHandle mojo_handle,
                                    const MojoUnwrapPlatformHandleOptions* options,
                                    MojoPlatformHandle* platform_handle)
{
    if (platform_handle) {
        platform_handle->struct_size = sizeof(MojoPlatformHandle);
        platform_handle->type = MOJO_PLATFORM_HANDLE_TYPE_INVALID;
        platform_handle->value = 0;
    }
    return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult MojoWrapPlatformSharedMemoryRegion(
    const MojoPlatformHandle* platform_handles,
    uint32_t num_platform_handles,
    uint64_t num_bytes,
    const MojoSharedBufferGuid* guid,
    MojoPlatformSharedMemoryRegionAccessMode access_mode,
    const MojoWrapPlatformSharedMemoryRegionOptions* options,
    MojoHandle* mojo_handle)
{
    if (mojo_handle)
        *mojo_handle = MOJO_HANDLE_INVALID;
    return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult MojoUnwrapPlatformSharedMemoryRegion(
    MojoHandle mojo_handle,
    const MojoUnwrapPlatformSharedMemoryRegionOptions* options,
    MojoPlatformHandle* platform_handles,
    uint32_t* num_platform_handles,
    uint64_t* num_bytes,
    MojoSharedBufferGuid* guid,
    MojoPlatformSharedMemoryRegionAccessMode* access_mode)
{
    if (num_platform_handles)
        *num_platform_handles = 0;
    if (num_bytes)
        *num_bytes = 0;
    if (guid) {
        guid->high = 0;
        guid->low = 0;
    }
    return MOJO_RESULT_UNIMPLEMENTED;
}

}  // extern "C"
