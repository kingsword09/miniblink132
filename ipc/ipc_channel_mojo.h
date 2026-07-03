// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPC_IPC_CHANNEL_MOJO_H_
#define IPC_IPC_CHANNEL_MOJO_H_

#include <memory>
#include <utility>

#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "ipc/ipc_listener.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/system/message_pipe.h"

namespace IPC {

// Lite mac compatibility for Chromium's Mojo IPC channel surface. miniblink's
// current Mojo layer aliases associated interfaces to normal message pipes, so
// the GPU host only needs this small API surface to construct and retain a
// channel object.
class ChannelMojo {
public:
    enum Mode {
        MODE_CLIENT = 0x2,
    };

    class AssociatedInterfaceSupport {
    public:
        template <typename Interface>
        void GetRemoteAssociatedInterface(mojo::PendingAssociatedReceiver<Interface> receiver)
        {
            receiver.reset();
        }
    };

    static std::unique_ptr<ChannelMojo> Create(mojo::ScopedMessagePipeHandle handle, Mode mode, Listener* listener,
        scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner, scoped_refptr<base::SingleThreadTaskRunner> proxy_task_runner)
    {
        return std::unique_ptr<ChannelMojo>(new ChannelMojo(std::move(handle), mode, listener, std::move(ipc_task_runner), std::move(proxy_task_runner)));
    }

    bool Connect()
    {
        return true;
    }

    AssociatedInterfaceSupport* GetAssociatedInterfaceSupport()
    {
        return &associated_interface_support_;
    }

private:
    ChannelMojo(mojo::ScopedMessagePipeHandle handle, Mode mode, Listener* listener, scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
        scoped_refptr<base::SingleThreadTaskRunner> proxy_task_runner)
        : handle_(std::move(handle))
        , mode_(mode)
        , listener_(listener)
        , ipc_task_runner_(std::move(ipc_task_runner))
        , proxy_task_runner_(std::move(proxy_task_runner))
    {
    }

    mojo::ScopedMessagePipeHandle handle_;
    Mode mode_;
    Listener* listener_;
    scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner_;
    scoped_refptr<base::SingleThreadTaskRunner> proxy_task_runner_;
    AssociatedInterfaceSupport associated_interface_support_;
};

} // namespace IPC

#endif // IPC_IPC_CHANNEL_MOJO_H_
