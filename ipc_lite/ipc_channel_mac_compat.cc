// Copyright 2026
// Minimal macOS compatibility for the legacy ipc_lite Channel surface.

#include "ipc_lite/ipc_channel.h"

#include <utility>

#include "base/process/process.h"
#include "ipc_lite/ipc_listener.h"

namespace IPC {

class Channel::ChannelImpl {
public:
    ChannelImpl(const IPC::ChannelHandle& channel_handle, Mode mode, Listener* listener)
        : channel_handle_(channel_handle), mode_(mode), listener_(listener), peer_pid_(base::GetCurrentProcId())
    {
    }

    bool Connect()
    {
        if (listener_)
            listener_->OnChannelConnected(peer_pid_);
        return true;
    }

    void Close() {}

    void set_listener(Listener* listener) { listener_ = listener; }

    bool Send(Message* message)
    {
        delete message;
        return true;
    }

    base::ProcessId peer_pid() const { return peer_pid_; }

    int GetClientFileDescriptor() const { return -1; }
    int TakeClientFileDescriptor() { return -1; }
    bool AcceptsConnections() const { return false; }
    bool HasAcceptedConnection() const { return false; }
    bool GetClientEuid(uid_t* client_euid) const { return false; }
    void ResetToAcceptingConnectionState() {}

private:
    IPC::ChannelHandle channel_handle_;
    Mode mode_;
    Listener* listener_;
    base::ProcessId peer_pid_;
};

Channel::Channel(const IPC::ChannelHandle& channel_handle, Mode mode, Listener* listener)
    : channel_impl_(new ChannelImpl(channel_handle, mode, listener))
{
}

Channel::~Channel()
{
    delete channel_impl_;
}

bool Channel::Connect()
{
    return channel_impl_->Connect();
}

void Channel::Close()
{
    channel_impl_->Close();
}

void Channel::set_listener(Listener* listener)
{
    channel_impl_->set_listener(listener);
}

base::ProcessId Channel::peer_pid() const
{
    return channel_impl_->peer_pid();
}

bool Channel::Send(Message* message)
{
    return channel_impl_->Send(message);
}

int Channel::GetClientFileDescriptor() const
{
    return channel_impl_->GetClientFileDescriptor();
}

int Channel::TakeClientFileDescriptor()
{
    return channel_impl_->TakeClientFileDescriptor();
}

bool Channel::AcceptsConnections() const
{
    return channel_impl_->AcceptsConnections();
}

bool Channel::HasAcceptedConnection() const
{
    return channel_impl_->HasAcceptedConnection();
}

bool Channel::GetClientEuid(uid_t* client_euid) const
{
    return channel_impl_->GetClientEuid(client_euid);
}

void Channel::ResetToAcceptingConnectionState()
{
    channel_impl_->ResetToAcceptingConnectionState();
}

bool Channel::IsNamedServerInitialized(const std::string& channel_id)
{
    return false;
}

} // namespace IPC
