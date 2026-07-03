// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/mediastream/user_media_client.h"

#include <utility>

#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"

namespace blink {

const char UserMediaClient::kSupplementName[] = "UserMediaClient";

UserMediaClient::UserMediaClient(
    LocalFrame* frame,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : UserMediaClient(frame, nullptr, nullptr, std::move(task_runner))
{
}

UserMediaClient::UserMediaClient(
    LocalFrame* frame,
    UserMediaProcessor*,
    UserMediaProcessor*,
    scoped_refptr<base::SingleThreadTaskRunner>)
    : Supplement<LocalDOMWindow>(*frame->DomWindow()),
      ExecutionContextLifecycleObserver(frame->DomWindow()),
      frame_(frame),
      media_devices_dispatcher_(frame->DomWindow())
{
}

void UserMediaClient::RequestUserMedia(UserMediaRequest*)
{
}

void UserMediaClient::CancelUserMediaRequest(UserMediaRequest*)
{
}

void UserMediaClient::ApplyConstraints(blink::ApplyConstraintsRequest*)
{
}

void UserMediaClient::StopTrack(MediaStreamComponent*)
{
}

void UserMediaClient::ContextDestroyed()
{
}

bool UserMediaClient::IsCapturing()
{
    return false;
}

UserMediaClient* UserMediaClient::From(LocalDOMWindow*)
{
    return nullptr;
}

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
void UserMediaClient::FocusCapturedSurface(const String&, bool)
{
}
#endif

void UserMediaClient::Trace(Visitor* visitor) const
{
    visitor->Trace(frame_);
    visitor->Trace(media_devices_dispatcher_);
    Supplement<LocalDOMWindow>::Trace(visitor);
    ExecutionContextLifecycleObserver::Trace(visitor);
}

void UserMediaClient::SetMediaDevicesDispatcherForTesting(
    mojo::PendingRemote<blink::mojom::blink::MediaDevicesDispatcherHost>)
{
}

void UserMediaClient::KeepDeviceAliveForTransfer(
    base::UnguessableToken,
    base::UnguessableToken,
    UserMediaProcessor::KeepDeviceAliveForTransferCallback keep_alive_cb)
{
    std::move(keep_alive_cb).Run(false);
}

}  // namespace blink
