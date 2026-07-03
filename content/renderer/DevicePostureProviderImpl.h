
#ifndef content_renderer_DevicePostureProviderImpl_h
#define content_renderer_DevicePostureProviderImpl_h

#include "gen/third_party/blink/public/mojom/device_posture/device_posture_provider.mojom-blink.h"
#include <windows.h>

namespace content {

class DevicePostureProviderImpl : public ::blink::mojom::blink::DevicePostureProvider {
public:
    /*virtual*/ ~DevicePostureProviderImpl() = default;

    //using AddListenerAndGetCurrentPostureCallback = base::OnceCallback<void(DevicePostureType)>;

    /*virtual*/ void AddListenerAndGetCurrentPosture(
        ::mojo::PendingRemote<::blink::mojom::blink::DevicePostureClient> client, AddListenerAndGetCurrentPostureCallback callback) override
    {
        std::move(callback).Run(::blink::mojom::blink::DevicePostureType::kContinuous);
    }

    /*virtual*/ void OverrideDevicePostureForEmulation(::blink::mojom::blink::DevicePostureType posture) override
    {
        (void)posture;
    }

    /*virtual*/ void DisableDevicePostureOverrideForEmulation() override
    {
    }
};

}

#endif // content_renderer_DevicePostureProviderImpl_h
