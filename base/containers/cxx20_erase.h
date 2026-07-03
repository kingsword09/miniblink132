// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CONTAINERS_CXX20_ERASE_H_
#define BASE_CONTAINERS_CXX20_ERASE_H_

#include <algorithm>
#include <cstddef>

namespace base {

template <typename Container, typename Value>
size_t Erase(Container& container, const Value& value)
{
    auto old_size = container.size();
    container.erase(std::remove(container.begin(), container.end(), value), container.end());
    return old_size - container.size();
}

template <typename Container, typename Predicate>
size_t EraseIf(Container& container, Predicate pred)
{
    auto old_size = container.size();
    container.erase(std::remove_if(container.begin(), container.end(), pred), container.end());
    return old_size - container.size();
}

} // namespace base

#endif // BASE_CONTAINERS_CXX20_ERASE_H_
