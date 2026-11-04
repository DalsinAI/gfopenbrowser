/*
 * GFOpenBrowser: one web page in a view: WebCore's Page with GFOpenBrowser's
 * clients, loading, painting into a 32-bit ARGB buffer, and input. The
 * Amiga window around it talks to it through ob_webview.h.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#pragma once

#include "ob_webview.h"

#include <WebCore/IntRect.h>
#include <WebCore/OpenGPUPaintAmiga.h>
#include <WebCore/IntSize.h>
#include <WebCore/Timer.h>
#include <wtf/Forward.h>
#include <wtf/RefPtr.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class LocalFrame;
class LocalFrameView;
class Page;
class ResourceError;
}

namespace GFOpenBrowser {

class AmigaBackForwardList;

class WebView {
    WTF_MAKE_NONCOPYABLE(WebView);
    WTF_MAKE_TZONE_ALLOCATED(WebView);
public:
    WebView(const OBWebViewCallbacks&, const WebCore::IntSize&);
    ~WebView();

    WebCore::Page& page() { return *m_page; }
    WebCore::LocalFrame* mainFrame() const;
    WebCore::LocalFrameView* mainFrameView() const;
    const WebCore::IntSize& size() const { return m_size; }

    // From the window.
    void load(const String& url);
    void loadHTML(const String& html, const String& baseURL);
    void loadData(const String& text, const String& mimeType, const String& baseURL);
    void goBack();
    void goForward();
    void reload();
    void stop();
    bool canGoBack() const;
    bool canGoForward() const;
    void resize(const WebCore::IntSize&);
    void setScriptsEnabled(bool);
    void setPicturesEnabled(bool);
    void setWebFontsEnabled(bool);
    void setLiteMode(bool enabled) { m_liteMode = enabled; }
    bool liteMode() const { return m_liteMode; }
    // layOut: lay the page out first if it must. Without, an area whose
    // layout is still due is not painted but kept dirty (returns false).
    bool paint(unsigned char* argb, int stride, const WebCore::IntRect&, bool layOut = true);
    void reportDisplayList(unsigned char* direct, unsigned char* replayed, int stride, const WebCore::IntRect&);
    // What painted the window since the last call, for OBVIEW_BENCH
    // (ob_webview_paint_report); then counts again from 0.
    int takePaintReport(char* buffer, int size);
    WebCore::IntRect takeDirtyRect();
    unsigned takeDirtyRects(WebCore::IntRect*, unsigned max);
    bool renderingUpdatePending() const { return m_renderingUpdateTimer.isActive(); }

    // From WebCore's clients.
    void invalidate(const WebCore::IntRect&);
    void scheduleRenderingUpdate();
    void setStatusText(const String&);
    void setTitle(const String&);
    void didCommitLoad(const String& url);
    void didStartLoad();
    void didFinishLoad();
    // The loads Tata's defer rules held for this page go now (ob_tata.h): the
    // page has painted and gone idle, or someone started on a form.
    void releaseDeferredLoads();
    void didFailLoad(const WebCore::ResourceError&);
    void setProgress(double);
    void contentsSizeChanged(const WebCore::IntSize&);
    void runAlert(const String&);
    bool runConfirm(const String&);
    bool runPrompt(const String& message, const String& defaultValue, String& result);
    // A server's log-in panel (the window's authenticate callback): true
    // with user and password filled to log in.
    bool runAuthentication(const String& host, const String& realm, unsigned previousFailures, String& user, String& password);
    // Find in the page (ob_webview_find).
    int find(const String&, int flags, int& current);
    void clearFind();
    void consoleMessage(const String& message, unsigned line, const String& source);
    void resourceStarted(const String& url);
    // A request of the page opened or closed (once each, redirects or not):
    // the network's idleness, for Tata's defer rules.
    void requestOpened();
    void requestClosed();
    void resourceEnded(const String& url, const String& error);
    // Bytes the page's requests have received so far (ob_webview_bytes_received).
    void addBytesReceived(int bytes) { if (bytes > 0) m_bytesReceived += static_cast<unsigned>(bytes); }
    unsigned long long bytesReceived() const { return m_bytesReceived; }

private:
    void renderingUpdateTimerFired();
    void deferTimerFired();
    void writeScriptCache();
    void scriptCacheTimerFired();
    void addDirtyRect(const WebCore::IntRect&);
    void callString(void (*callback)(void*, const char*), const String&);

    OBWebViewCallbacks m_callbacks;
    WebCore::IntSize m_size;
    RefPtr<WebCore::Page> m_page;
    RefPtr<AmigaBackForwardList> m_backForwardList;
    static constexpr unsigned maxDirtyRects = 8;
    Vector<WebCore::IntRect, maxDirtyRects> m_dirtyRects; // apart from each other
    WebCore::Timer m_deferTimer; // releases the loads Tata's defer rules held (releaseDeferredLoads())
    MonotonicTime m_firstPaintAt; // the page's first rendering update, for the defer rules
    MonotonicTime m_lastRequestChange; // a request started or ended, for the defer rules
    int m_openRequests { 0 };
    bool m_deferReleased { false };
    WebCore::Timer m_renderingUpdateTimer;
    WebCore::Timer m_scriptCacheTimer; // writes the translated scripts a while after a load, one file each idle tick
    bool m_scriptCacheHandedOver { false }; // this load's scripts went to the writer (writeScriptCache())
    MonotonicTime m_nextRenderingUpdate; // while a page loads, none before this
    unsigned m_renderingUpdates { 0 };
    unsigned m_paintedRenderingUpdate { 0 }; // the last one takeDirtyRects() gave out
    MonotonicTime m_nextLoadingPaint; // while a page loads, without a new rendering update
    std::optional<unsigned> m_layoutHeldSince; // m_renderingUpdates when areas began to wait for layout
    bool m_layOutForPaint { false }; // the areas given out are painted laid out first
    bool m_loading { false };
    bool m_liteMode { false };
    unsigned long long m_bytesReceived { 0 };
    obgpu_counts m_paintCounts { }; // GFOpenGPU's draws and batches, and the calls cairo drew
    unsigned long m_paintMicroseconds { 0 }; // time in paint() since the last report
};

} // namespace GFOpenBrowser
