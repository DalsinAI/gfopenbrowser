/*
 * GFOpenBrowser: downloads. Each one is its own request (a ResourceHandle,
 * so it has the page's cookies and goes through Tata like any load), written
 * to a file in the downloads folder as its bytes arrive. The window hears
 * of each one through ob_download_set_callback() (ob_webview.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#pragma once

#include <wtf/Forward.h>

namespace WebCore {
class ResourceRequest;
class ResourceResponse;
}

namespace GFOpenBrowser {

// Starts a download of request (a link with a download attribute, a page
// whose type can't be shown, or one the window asked for); suggestedName is
// the file's name when the server gives none. Returns its number, 0 when it
// could not start.
int startDownload(const WebCore::ResourceRequest&, const String& suggestedName);

} // namespace GFOpenBrowser
