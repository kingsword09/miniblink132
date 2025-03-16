
#include "content/renderer/AssociatedInterfaceProviderImpl.h"
#include "content/common/CreateAndBindTempl.h"
#include "content/renderer/ScreenOrientationImpl.h"
#include "content/renderer/WebLocalFrameClientImpl.h"
#include <windows.h>

namespace content {

AssociatedInterfaceProviderImpl::AssociatedInterfaceProviderImpl(WebLocalFrameClientImpl* frameClient)
    //: m_localFrameHostReceiver(new LocalFrameHostImpl(frameClient))
    //, m_backForwardCacheControllerHostReceiver(new BackForwardCacheControllerHostImpl())
    //, m_localMainFrameHostReceiver(new LocalMainFrameHostImpl())
    //, m_broadcastChannelProviderReceiver(new BroadcastChannelProviderImpl(frameClient))
    : m_frameClient(frameClient)
{
}

void AssociatedInterfaceProviderImpl::GetAssociatedInterface(
    const std::string& name, ::mojo::PendingAssociatedReceiver<blink::mojom::AssociatedInterface> receiver)
{
    std::string output("AssociatedInterfaceProviderImpl::GetAssociatedInterface: ");
    output += name;
    output += "\n";
    OutputDebugStringA(output.c_str());

    if ("blink.mojom.BackForwardCacheControllerHost" == name) {
        //mojo::PendingAssociatedReceiver<::blink::mojom::blink::BackForwardCacheControllerHost> pendingReceiver(receiver.PassHandle());
        //m_backForwardCacheControllerHostReceiver.Bind(std::move(pendingReceiver));
        createAndBindInterface<::blink::mojom::blink::BackForwardCacheControllerHost, BackForwardCacheControllerHostImpl>(receiver.PassPipe());
    } else if ("blink.mojom.LocalFrameHost" == name) {
        //mojo::PendingAssociatedReceiver<::blink::mojom::blink::LocalFrameHost> pendingReceiver(receiver.PassHandle());
        //m_localFrameHostReceiver.Bind(std::move(pendingReceiver));
        createAndBindInterface<::blink::mojom::blink::LocalFrameHost, LocalFrameHostImpl>(receiver.PassPipe(), m_frameClient);
    } else if ("blink.mojom.LocalMainFrameHost" == name) {
        //mojo::PendingAssociatedReceiver<::blink::mojom::blink::LocalMainFrameHost> pendingReceiver(receiver.PassHandle());
        //m_localMainFrameHostReceiver.Bind(std::move(pendingReceiver));
        createAndBindInterface<::blink::mojom::blink::LocalMainFrameHost, LocalMainFrameHostImpl>(receiver.PassPipe(), m_frameClient);
    } else if ("blink.mojom.BroadcastChannelProvider" == name) {
        //mojo::PendingAssociatedReceiver<::blink::mojom::blink::BroadcastChannelProvider> pendingReceiver(receiver.PassHandle());
        //m_broadcastChannelProviderReceiver.Bind(std::move(pendingReceiver));
        createAndBindInterface<::blink::mojom::blink::BroadcastChannelProvider, BroadcastChannelProviderImpl>(receiver.PassPipe(), m_frameClient);
    } else if ("device.mojom.blink.ScreenOrientation" == name) {
        createAndBindInterface<::device::mojom::blink::ScreenOrientation, ScreenOrientationImpl>(receiver.PassPipe());
    } else
        DebugBreak();
}

}