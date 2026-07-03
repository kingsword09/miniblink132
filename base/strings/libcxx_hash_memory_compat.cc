// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>

namespace std {
inline namespace __1 {

size_t __hash_memory(const void* data, size_t length)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace __1
}  // namespace std
