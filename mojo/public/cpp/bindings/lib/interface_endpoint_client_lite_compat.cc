// Copyright 2026
// Minimal miniblink lite compatibility for Mojo control-message idle handling.

#include "mojo/public/cpp/bindings/interface_endpoint_client.h"

#include <utility>

#include "base/functional/bind.h"

namespace mojo {

bool InterfaceEndpointClient::AcceptEnableIdleTracking(base::TimeDelta timeout)
{
    if (idle_tracking_enabled_callback_) {
        std::move(idle_tracking_enabled_callback_).Run(ConnectionGroup::Ref());
    }

    idle_timeout_ = timeout;
    return true;
}

bool InterfaceEndpointClient::AcceptMessageAck()
{
    if (!idle_handler_ || num_unacked_messages_ == 0)
        return false;

    --num_unacked_messages_;
    return true;
}

bool InterfaceEndpointClient::AcceptNotifyIdle()
{
    if (!idle_handler_)
        return false;

    if (num_unacked_messages_ > 0)
        return true;

    idle_handler_.Run();
    return true;
}

void InterfaceEndpointClient::MaybeStartIdleTimer()
{
}

} // namespace mojo
