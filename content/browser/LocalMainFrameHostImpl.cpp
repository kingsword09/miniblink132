
#include "content/browser/LocalMainFrameHostImpl.h"

#include "content/browser/MbWebview.h"
#include "content/renderer/WebLocalFrameClientImpl.h"

namespace content {

void LocalMainFrameHostImpl::DidFirstVisuallyNonEmptyPaint()
{
    m_frameClient->onLoadingSucceeded();
}

}
