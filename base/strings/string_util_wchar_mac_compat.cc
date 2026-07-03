// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/string_util.h"

#include "base/strings/string_util_impl_helpers.h"

namespace base {

bool IsStringASCII(std::wstring_view str)
{
    return internal::DoIsStringASCII(str.data(), str.length());
}

}  // namespace base
