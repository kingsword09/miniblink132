// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MAC_SCOPED_MACH_PORT_H_
#define BASE_MAC_SCOPED_MACH_PORT_H_

#include "base/apple/scoped_mach_port.h"

namespace base::mac {

using ScopedMachPortSet = base::apple::ScopedMachPortSet;
using ScopedMachReceiveRight = base::apple::ScopedMachReceiveRight;
using ScopedMachSendRight = base::apple::ScopedMachSendRight;

using base::apple::CreateMachPort;
using base::apple::RetainMachSendRight;

} // namespace base::mac

#endif // BASE_MAC_SCOPED_MACH_PORT_H_
