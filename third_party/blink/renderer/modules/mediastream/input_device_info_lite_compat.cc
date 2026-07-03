// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/mediastream/input_device_info.h"

#include "third_party/blink/public/mojom/mediastream/media_devices.mojom-blink.h"

namespace blink {

InputDeviceInfo::InputDeviceInfo(const String& device_id,
                                 const String& label,
                                 const String& group_id,
                                 mojom::blink::MediaDeviceType device_type)
    : MediaDeviceInfo(device_id, label, group_id, device_type)
{
}

void InputDeviceInfo::SetVideoInputCapabilities(
    mojom::blink::VideoInputDeviceCapabilitiesPtr)
{
}

void InputDeviceInfo::SetAudioInputCapabilities(
    mojom::blink::AudioInputDeviceCapabilitiesPtr)
{
}

MediaTrackCapabilities* InputDeviceInfo::getCapabilities() const
{
    return nullptr;
}

}  // namespace blink
