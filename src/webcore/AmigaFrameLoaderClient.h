/*
 * GFOpenBrowser: WebCore's frame loader client (one per frame).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#pragma once

#include <wtf/UniqueRef.h>

namespace WebCore {
class FrameLoader;
class LocalFrameLoaderClient;
}

namespace GFOpenBrowser {

class WebView;

UniqueRef<WebCore::LocalFrameLoaderClient> createFrameLoaderClient(WebCore::FrameLoader&, WebView&);

} // namespace GFOpenBrowser
