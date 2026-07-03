// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/host/host_display_client.h"

#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include <utility>

#include "components/viz/common/display/use_layered_window.h"
#include "components/viz/host/layered_window_updater_impl.h"
#include "ui/base/win/internal_constants.h"
#endif

namespace viz {

HostDisplayClient::HostDisplayClient(gfx::AcceleratedWidget widget)
{
#if BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_WIN)
    widget_ = widget;
#endif
}

HostDisplayClient::~HostDisplayClient() = default;

mojo::PendingRemote<mojom::DisplayClient> HostDisplayClient::GetBoundRemote(scoped_refptr<base::SingleThreadTaskRunner> task_runner)
{
    return receiver_.BindNewPipeAndPassRemote(task_runner);
}

void HostDisplayClient::CreateLayeredWindowUpdater(mojo::PendingReceiver<mojom::LayeredWindowUpdater> receiver)
{
#if BUILDFLAG(IS_WIN)
    if (!NeedsToUseLayerWindow(widget_)) {
        DLOG(ERROR) << "HWND shouldn't be using a layered window";
        return;
    }

    layered_window_updater_ = std::make_unique<LayeredWindowUpdaterImpl>(widget_, std::move(receiver));
#else
    NOTIMPLEMENTED();
#endif
}

void HostDisplayClient::AddChildWindowToBrowser(gpu::SurfaceHandle child_window)
{
#if BUILDFLAG(IS_WIN)
    NOTREACHED_IN_MIGRATION();
#else
    NOTIMPLEMENTED();
#endif
}

#if BUILDFLAG(IS_LINUX) && BUILDFLAG(IS_OZONE_X11)
void HostDisplayClient::DidCompleteSwapWithNewSize(const gfx::Size& size)
{
    NOTIMPLEMENTED();
}
#endif // BUILDFLAG(IS_LINUX) && BUILDFLAG(IS_OZONE_X11)

#if BUILDFLAG(IS_CHROMEOS_ASH)
void HostDisplayClient::SetPreferredRefreshRate(float refresh_rate)
{
    NOTREACHED();
}
#endif // BUILDFLAG(IS_CHROMEOS_ASH)

} // namespace viz
