/*
 * GFOpenBrowser: WebCore's chrome client, the page's link to its window:
 * repaints, the window's size, status text, JavaScript dialogs and the
 * rendering updates WebCore asks for.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#pragma once

#include <wtf/UniqueRef.h>

namespace WebCore {
class ChromeClient;
}

namespace GFOpenBrowser {

class WebView;

UniqueRef<WebCore::ChromeClient> createChromeClient(WebView&);

} // namespace GFOpenBrowser
