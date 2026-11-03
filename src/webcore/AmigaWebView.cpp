/*
 * GFOpenBrowser: one web page in a view (see AmigaWebView.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "config.h"
#include "AmigaWebView.h"

#include "AmigaBackForwardList.h"
#include "AmigaCookieJar.h"
#include "AmigaChromeClient.h"
#include "AmigaEditorClient.h"
#include "AmigaFrameLoaderClient.h"
#include "AmigaSessionStorage.h"

#include <WebCore/DocumentPage.h>
#include <WebCore/DocumentView.h>
#include <WebCore/FrameDestructionObserverInlines.h>
#include <WebCore/NodeDocument.h>
#include <WebCore/BackForwardController.h>
#include <WebCore/Document.h>
#include <WebCore/DisplayList.h>
#include <WebCore/DisplayListItems.h>
#include <WebCore/DisplayListRecorderImpl.h>
#include <WebCore/DocumentLoader.h>
#include <WebCore/EditorClient.h>
#include <WebCore/EmptyClients.h>
#include <WebCore/FocusController.h>
#include <WebCore/ForcedAccessibilityValue.h>
#include <WebCore/FrameLoadRequest.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/AmigaImage.h>
#include <WebCore/GraphicsContextOpenGPU.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h>
#include <WebCore/LocalFrameLoaderClient.h>
#include <WebCore/LocalFrameView.h>
#include <WebCore/Page.h>
#include <WebCore/PageConfiguration.h>
#include <WebCore/ProgressTracker.h>
#include <WebCore/ProgressTrackerClient.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/Settings.h>
#include <WebCore/SharedBuffer.h>
#include <WebCore/SubstituteData.h>
#include <WebCore/TrustedFonts.h>
#include <stdio.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/CString.h>

namespace GFOpenBrowser {

using namespace WebCore;

class AmigaProgressTrackerClient final : public ProgressTrackerClient {
public:
    explicit AmigaProgressTrackerClient(WebView& view)
        : m_view(view)
    {
    }

private:
    void progressStarted(LocalFrame& frame) final { report(frame); }
    void progressEstimateChanged(LocalFrame& frame) final { report(frame); }
    void progressFinished(LocalFrame& frame) final { report(frame); }

    void report(LocalFrame& frame)
    {
        if (RefPtr page = frame.page())
            m_view.setProgress(page->progress().estimatedProgress());
    }

    WebView& m_view;
};

template<typename UTF8String>
static const char* cString(const UTF8String& string)
{
    return reinterpret_cast<const char*>(string.data());
}

static void resetFPCR()
{
    // ROM math libraries can leave the FPU in single precision; WebCore
    // lays out and paints with doubles.
    __asm__ volatile ("fmove.l %0,%%fpcr" : : "d" (0));
}

WTF_MAKE_TZONE_ALLOCATED_IMPL(WebView);

WebView::WebView(const OBWebViewCallbacks& callbacks, const IntSize& size)
    : m_callbacks(callbacks)
    , m_size(size)
    , m_renderingUpdateTimer([this] { renderingUpdateTimerFired(); })
{
    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, PAL::SessionID::defaultSessionID());
    configuration.chromeClient = createChromeClient(*this);
    configuration.editorClient = createEditorClient();
    configuration.progressTrackerClient = makeUniqueRef<AmigaProgressTrackerClient>(*this);
    if (auto* jar = AmigaCookieJar::shared())
        configuration.cookieJar = *jar;
    m_backForwardList = AmigaBackForwardList::create();
    configuration.backForwardClient = *m_backForwardList;
    configuration.storageNamespaceProvider = createSessionStorageProvider();
    WebView* view = this;
    configuration.mainFrameCreationParameters = PageConfiguration::LocalMainFrameCreationParameters {
        CompletionHandler<UniqueRef<LocalFrameLoaderClient>(LocalFrame&, FrameLoader&)> { [view](auto&, auto& frameLoader) {
            return createFrameLoaderClient(frameLoader, *view);
        } },
        SandboxFlags { },
        ReferrerPolicy::EmptyString
    };

    m_page = Page::create(WTF::move(configuration));
    auto& settings = m_page->settings();
    settings.setScriptEnabled(true);
    settings.setLoadsImagesAutomatically(true);
    settings.setAcceleratedCompositingEnabled(false);
    settings.setStandardFontFamily("Liberation Sans"_s);
    settings.setSansSerifFontFamily("Liberation Sans"_s);
    settings.setSerifFontFamily("Liberation Serif"_s);
    settings.setFixedFontFamily("Liberation Mono"_s);
    settings.setDefaultFontSize(16);
    settings.setDefaultFixedFontSize(13);
    settings.setMinimumLogicalFontSize(9);
    // Less work for a 68k, nothing a page needs: no connections opened in
    // advance (each TLS handshake takes seconds), no back/forward cache, no
    // animation where a page offers to do without, pictures marked
    // loading="lazy" loaded when they come into view, and the fonts on the
    // disk rather than downloaded ones (setWebFontsEnabled()). Pictures come
    // from datatypes, which give an animation's first frame only.
    settings.setLinkPreconnectEnabled(false);
    settings.setUsesBackForwardCache(false);
    settings.setForcedPrefersReducedMotionAccessibilityValue(ForcedAccessibilityValue::On);
    settings.setLazyImageLoadingEnabled(true);
    settings.setDownloadableBinaryFontTrustedTypes(DownloadableBinaryFontTrustedTypes::None);
    // Pictures decode when they are drawn, on this task. Decoding them on
    // other threads gains nothing on one CPU, costs each picture a thread
    // with a 2 MB stack, and those threads never end, so the program could
    // not exit (libpthread waits for every thread at exit).
    settings.setLargeImageAsyncDecodingEnabled(false);
    settings.setAnimatedImageAsyncDecodingEnabled(false);
    // No IndexedDB store yet: WebCore's empty database provider aborts the
    // moment a page opens one (youtube.com). Without the API, pages carry on.
    settings.setIndexedDBAPIEnabled(false);
    // sessionStorage, in memory for the life of the view (AmigaSessionStorage):
    // pages (reddit.com) read window.sessionStorage without checking for it.
    settings.setSessionStorageEnabled(true);

    RefPtr frame = m_page->localMainFrame();
    frame->init();
    m_page->setIsVisible(true);
    m_page->setIsInWindow(true);
    m_page->focusController().setActive(true);
    m_page->focusController().setFocused(true);
}

WebView::~WebView()
{
    m_renderingUpdateTimer.stop();
    if (RefPtr frame = mainFrame())
        frame->loader().detachFromParent();
    m_page = nullptr;
}

LocalFrame* WebView::mainFrame() const
{
    return m_page ? m_page->localMainFrame() : nullptr;
}

LocalFrameView* WebView::mainFrameView() const
{
    RefPtr frame = mainFrame();
    return frame ? frame->view() : nullptr;
}

void WebView::load(const String& urlString)
{
    RefPtr frame = mainFrame();
    if (!frame)
        return;
    URL url { urlString };
    if (!url.isValid())
        url = URL { makeString("https://"_s, urlString) };
    frame->loader().load(FrameLoadRequest(*frame, ResourceRequest(WTF::move(url))));
}

void WebView::loadHTML(const String& html, const String& baseURL)
{
    loadData(html, "text/html"_s, baseURL);
}

void WebView::loadData(const String& text, const String& mimeType, const String& baseURL)
{
    RefPtr frame = mainFrame();
    if (!frame)
        return;
    CString utf8 = text.utf8();
    URL base { baseURL.isEmpty() ? "about:blank"_s : baseURL };
    ResourceResponse response(URL { base }, mimeType.isEmpty() ? String { "text/html"_s } : String { mimeType }, utf8.length(), "UTF-8"_s);
    SubstituteData data(SharedBuffer::create(utf8.span()), URL { }, WTF::move(response), SubstituteData::SessionHistoryVisibility::Hidden);
    frame->loader().load(FrameLoadRequest(*frame, ResourceRequest(WTF::move(base)), WTF::move(data)));
}

void WebView::goBack()
{
    if (m_page)
        m_page->backForward().goBack();
}

void WebView::goForward()
{
    if (m_page)
        m_page->backForward().goForward();
}

void WebView::reload()
{
    if (RefPtr frame = mainFrame())
        frame->loader().reload();
}

void WebView::stop()
{
    if (RefPtr frame = mainFrame())
        frame->loader().stopAllLoaders();
}

bool WebView::canGoBack() const
{
    return m_page && m_page->backForward().canGoBackOrForward(-1);
}

bool WebView::canGoForward() const
{
    return m_page && m_page->backForward().canGoBackOrForward(1);
}

void WebView::resize(const IntSize& size)
{
    m_size = size;
    if (RefPtr view = mainFrameView())
        view->resize(size);
    invalidate(IntRect(IntPoint(), size));
}

// Whether the view itself covers every pixel it paints: with its base
// background (white and opaque, from AmigaFrameLoaderClient's createView())
// under the page. Not before it has a document laid out, or while the
// document holds its visual updates back: then nothing is painted at all.
static bool paintsWholeArea(LocalFrameView& view)
{
    if (!view.renderView() || view.needsLayout() || view.isTransparent() || !view.baseBackgroundColor().isOpaque())
        return false;
    RefPtr document = view.frame().document();
    return document && document->visualUpdatesAllowed();
}

bool WebView::paint(unsigned char* argb, int stride, const IntRect& rect, bool layOut)
{
    RefPtr view = mainFrameView();
    IntRect area = intersection(rect, IntRect(IntPoint(), m_size));
    if (area.isEmpty())
        return true;
    resetFPCR();
    if (view && (layOut || m_layOutForPaint))
        view->updateLayoutAndStyleIfNeededRecursive();
    else if (view && view->needsLayout()) {
        // WebCore paints nothing where layout is due; the rendering update
        // lays out, and the area is painted after it.
        addDirtyRect(area);
        return false;
    }
    // The window's pixels as a picture of their own (WebKit patch 0083: no
    // cairo); everything is drawn through GFOpenGPU, and the context draws
    // the last batch as it goes.
    auto pixels = AmigaImage::wrap(argb + area.y() * stride + area.x() * 4, area.size(), stride, true, [] { });
    auto start = MonotonicTime::now();
    {
        GraphicsContextOpenGPU context(pixels.get(), &m_paintCounts);
        // Scaled pictures with the cheapest filter: a 68k has no time for more.
        context.setImageInterpolationQuality(InterpolationQuality::Low);
        context.translate(-area.x(), -area.y());
        if (!view || !paintsWholeArea(*view))
            context.fillRect(area, Color::white);
        if (view)
            view->paint(context, area);
    }
    m_paintMicroseconds += static_cast<unsigned long>((MonotonicTime::now() - start).microseconds());
    return true;
}

int WebView::takePaintReport(char* buffer, int size)
{
    int used = 0;
    if (buffer && size > 0) {
        buffer[0] = 0;
        if (!GraphicsContextOpenGPU::isWanted())
            used = snprintf(buffer, size, " paint_gpu=0 paint_us=%lu", m_paintMicroseconds);
        else {
            unsigned long handed = 0;
            for (auto calls : m_paintCounts.handed)
                handed += calls;
            // cairo_calls: the calls not drawn here (the name the tests read
            // since 0076, when cairo drew them)
            used = snprintf(buffer, size, " paint_gpu=1 paint_us=%lu gpu_draws=%lu gpu_flushes=%lu cairo_calls=%lu",
                m_paintMicroseconds, m_paintCounts.draws, m_paintCounts.flushes, handed);
            for (int kind = 0; kind < OBGPU_KINDS && used >= 0 && used < size; kind++) {
                if (m_paintCounts.handed[kind])
                    used += snprintf(buffer + used, size - used, " cairo_%s=%lu", obgpu_kind_name(kind), m_paintCounts.handed[kind]);
            }
        }
        if (used < 0)
            used = 0;
        if (used >= size)
            used = size - 1;
    }
    m_paintCounts = { };
    m_paintMicroseconds = 0;
    return used;
}

// The display-list experiment (5 October 2026): paint the area as today
// into `direct`; then into WebKit's display-list recorder, which keeps the
// drawing commands (rectangles, glyphs, pictures) instead of pixels; then
// replay those commands through GFOpenGPU into `replayed`. It prints
// how long each step took, what the commands are and roughly how many bytes
// they would take to send to something else to draw: a GPU, or the host.
void WebView::reportDisplayList(unsigned char* direct, unsigned char* replayed, int stride, const IntRect& rect)
{
    RefPtr view = mainFrameView();
    IntRect area = intersection(rect, IntRect(IntPoint(), m_size));
    if (!view || area.isEmpty())
        return;
    resetFPCR();
    view->updateLayoutAndStyleIfNeededRecursive();

    auto start = MonotonicTime::now();
    paint(direct, stride, area);
    Seconds paintTime = MonotonicTime::now() - start;

    start = MonotonicTime::now();
    DisplayList::RecorderImpl recorder { FloatRect(area) };
    recorder.fillRect(area, Color::white);
    view->paint(recorder, area);
    Ref list = recorder.takeDisplayList();
    Seconds recordTime = MonotonicTime::now() - start;

    // Each command as a type number and its numbers (a rectangle, a colour,
    // a transform): 16 bytes or so; a glyph run adds a glyph number and an
    // advance per glyph. Pictures would be sent once and then named.
    // Text comes as nested lists (WebKit keeps each run of glyphs as a small
    // display list of its own): count inside them too.
    Vector<std::pair<const char*, unsigned>> counts;
    size_t commands = 0, glyphs = 0, pictures = 0, bytes = 0;
    double picturePixels = 0;
    Function<void(const DisplayList::DisplayList&)> count = [&](const DisplayList::DisplayList& displayList) {
        for (auto& item : displayList.items()) {
            const char* name = nullptr;
            WTF::switchOn(item, [&]<typename ItemType>(const ItemType&) { name = ItemType::name; });
            auto found = counts.findIf([&](auto& entry) { return entry.first == name; });
            if (found == notFound)
                counts.append({ name, 1 });
            else
                counts[found].second++;
            commands++;
            bytes += 16;
            if (auto* drawGlyphs = std::get_if<DisplayList::DrawGlyphs>(&item)) {
                glyphs += drawGlyphs->length();
                bytes += drawGlyphs->length() * 6;
            } else if (auto* drawImage = std::get_if<DisplayList::DrawNativeImage>(&item)) {
                pictures++;
                picturePixels += drawImage->destinationRect().width() * drawImage->destinationRect().height();
            } else if (auto* nested = std::get_if<DisplayList::DrawDisplayList>(&item))
                count(nested->displayList().get());
        }
    };
    count(list.get());

    start = MonotonicTime::now();
    auto pixels = AmigaImage::wrap(replayed + area.y() * stride + area.x() * 4, area.size(), stride, true, [] { });
    {
        GraphicsContextOpenGPU context(pixels.get(), nullptr);
        context.setImageInterpolationQuality(InterpolationQuality::Low);
        context.translate(-area.x(), -area.y());
        context.drawDisplayList(list.get());
    }
    Seconds replayTime = MonotonicTime::now() - start;

    printf("OBVIEW_DL area=%dx%d pixel_bytes=%d commands=%zu glyphs=%zu pictures=%zu picture_pixels=%.0f command_bytes=%zu "
        "paint_ms=%.0f record_ms=%.0f replay_ms=%.0f\n", area.width(), area.height(), area.width() * area.height() * 4,
        commands, glyphs, pictures, picturePixels, bytes, paintTime.milliseconds(), recordTime.milliseconds(),
        replayTime.milliseconds());
    std::ranges::sort(counts, [](auto& a, auto& b) { return a.second > b.second; });
    for (auto& [name, count] : counts)
        printf("OBVIEW_DL_COMMAND %s %u\n", name, count);
    fflush(stdout);
}

IntRect WebView::takeDirtyRect()
{
    IntRect dirty;
    for (auto& rect : m_dirtyRects)
        dirty.unite(rect);
    m_dirtyRects.clear();
    return intersection(dirty, IntRect(IntPoint(), m_size));
}

// The window's repaint: the areas changed, as long as the page is laid out.
// Layout is the rendering update's (renderingUpdateTimerFired()), which is
// spaced out while a page loads; until it has run the areas wait, and while
// loading they are given out once per rendering update, or after a second
// when only pictures arrived. Once loaded, at once. Should rendering updates
// leave the layout due (two have run while the areas waited), the areas are
// given out to be painted laid out first, as before, rather than wait for ever.
unsigned WebView::takeDirtyRects(IntRect* rects, unsigned max)
{
    m_layOutForPaint = false;
    if (m_dirtyRects.isEmpty() || !max)
        return 0;
    if (RefPtr view = mainFrameView(); view && view->needsLayout()) {
        if (!m_layoutHeldSince)
            m_layoutHeldSince = m_renderingUpdates;
        if (m_renderingUpdates - *m_layoutHeldSince < 2) {
            if (!m_renderingUpdateTimer.isActive())
                scheduleRenderingUpdate();
            return 0;
        }
        m_layOutForPaint = true;
    }
    if (m_loading) {
        auto now = MonotonicTime::now();
        if (m_paintedRenderingUpdate == m_renderingUpdates && now < m_nextLoadingPaint)
            return 0;
        m_nextLoadingPaint = now + 1_s;
    }
    m_layoutHeldSince = std::nullopt;
    m_paintedRenderingUpdate = m_renderingUpdates;
    unsigned count = 0;
    if (m_dirtyRects.size() > max)
        rects[count++] = takeDirtyRect();
    else {
        for (auto& rect : m_dirtyRects)
            rects[count++] = rect;
        m_dirtyRects.clear();
    }
    return count;
}

// A few areas apart (a picture here, a line of text there) are kept apart,
// to be painted and copied one by one; areas that overlap become one. Past
// maxDirtyRects, one area around them all.
void WebView::addDirtyRect(const IntRect& rect)
{
    IntRect area = intersection(rect, IntRect(IntPoint(), m_size));
    if (area.isEmpty())
        return;
    for (size_t i = 0; i < m_dirtyRects.size();) {
        if (m_dirtyRects[i].contains(area))
            return;
        if (m_dirtyRects[i].intersects(area)) {
            area.unite(m_dirtyRects[i]);
            m_dirtyRects.removeAt(i);
            i = 0;
        } else
            i++;
    }
    if (m_dirtyRects.size() == maxDirtyRects) {
        for (auto& dirty : m_dirtyRects)
            area.unite(dirty);
        m_dirtyRects.clear();
    }
    m_dirtyRects.append(area);
}

void WebView::invalidate(const IntRect& rect)
{
    addDirtyRect(rect);
    if (m_callbacks.invalidate)
        m_callbacks.invalidate(m_callbacks.context, rect.x(), rect.y(), rect.width(), rect.height());
}

void WebView::scheduleRenderingUpdate()
{
    if (m_renderingUpdateTimer.isActive())
        return;
    // While a page loads, WebCore asks for a rendering update (style and
    // layout of the whole page) after each piece of it arrives. On a 68k that
    // is most of the work, so until the page has loaded the updates are
    // spaced out: see renderingUpdateTimerFired().
    Seconds delay = 0_s;
    if (m_loading)
        delay = std::max(0_s, m_nextRenderingUpdate - MonotonicTime::now());
    m_renderingUpdateTimer.startOneShot(delay);
}

void WebView::renderingUpdateTimerFired()
{
    if (!m_page)
        return;
    resetFPCR();
    auto start = MonotonicTime::now();
    m_page->updateRendering();
    m_page->finalizeRenderingUpdate({ });
    m_renderingUpdates++;
    // The next one while loading: a second from now, or twice as long as
    // this one took, so loading keeps at least two thirds of the time.
    auto end = MonotonicTime::now();
    m_nextRenderingUpdate = end + std::max(1_s, (end - start) * 2);
    // The page is laid out now: the window's moment to paint what changed.
    if (m_callbacks.rendered)
        m_callbacks.rendered(m_callbacks.context);
}

void WebView::setScriptsEnabled(bool enabled)
{
    if (m_page)
        m_page->settings().setScriptEnabled(enabled);
}

void WebView::setWebFontsEnabled(bool enabled)
{
    if (m_page)
        m_page->settings().setDownloadableBinaryFontTrustedTypes(enabled ? DownloadableBinaryFontTrustedTypes::Any : DownloadableBinaryFontTrustedTypes::None);
}

void WebView::setPicturesEnabled(bool enabled)
{
    if (!m_page)
        return;
    m_page->settings().setLoadsImagesAutomatically(enabled);
    m_page->settings().setImagesEnabled(enabled);
}

void WebView::callString(void (*callback)(void*, const char*), const String& text)
{
    if (!callback)
        return;
    CString utf8 = text.utf8();
    callback(m_callbacks.context, cString(utf8));
}

void WebView::setStatusText(const String& text)
{
    callString(m_callbacks.status, text);
}

void WebView::setTitle(const String& title)
{
    callString(m_callbacks.title, title);
}

void WebView::didCommitLoad(const String& url)
{
    callString(m_callbacks.url, url);
}

void WebView::didStartLoad()
{
    m_loading = true;
    if (m_callbacks.loading)
        m_callbacks.loading(m_callbacks.context, 1, 0);
}

void WebView::didFinishLoad()
{
    m_loading = false;
    // A rendering update held back while loading is due now.
    if (m_renderingUpdateTimer.isActive())
        m_renderingUpdateTimer.startOneShot(0_s);
    if (m_callbacks.loading)
        m_callbacks.loading(m_callbacks.context, 0, 100);
}

void WebView::didFailLoad(const ResourceError& error)
{
    m_loading = false;
    if (m_renderingUpdateTimer.isActive())
        m_renderingUpdateTimer.startOneShot(0_s);
    if (m_callbacks.loading)
        m_callbacks.loading(m_callbacks.context, 0, 100);
    if (error.isCancellation() || !m_callbacks.failed)
        return;
    CString url = error.failingURL().string().utf8();
    CString description = error.localizedDescription().utf8();
    m_callbacks.failed(m_callbacks.context, cString(url), cString(description));
}

void WebView::setProgress(double progress)
{
    if (m_callbacks.loading)
        m_callbacks.loading(m_callbacks.context, m_loading ? 1 : 0, static_cast<int>(progress * 100));
}

void WebView::contentsSizeChanged(const IntSize&)
{
}

void WebView::runAlert(const String& message)
{
    callString(m_callbacks.alert, message);
}

bool WebView::runConfirm(const String& message)
{
    if (!m_callbacks.confirm)
        return true;
    CString utf8 = message.utf8();
    return m_callbacks.confirm(m_callbacks.context, cString(utf8));
}

bool WebView::runPrompt(const String& message, const String& defaultValue, String& result)
{
    if (!m_callbacks.prompt)
        return false;
    char buffer[1024];
    CString utf8Message = message.utf8();
    CString utf8Default = defaultValue.utf8();
    buffer[0] = 0;
    if (!m_callbacks.prompt(m_callbacks.context, cString(utf8Message), cString(utf8Default), buffer, sizeof buffer))
        return false;
    buffer[sizeof buffer - 1] = 0;
    result = String::fromUTF8(buffer);
    return true;
}

void WebView::consoleMessage(const String& message, unsigned line, const String& source)
{
    if (!m_callbacks.console)
        return;
    CString utf8Message = message.utf8();
    CString utf8Source = source.utf8();
    m_callbacks.console(m_callbacks.context, cString(utf8Message), static_cast<int>(line), cString(utf8Source));
}

void WebView::resourceStarted(const String& url)
{
    if (!m_callbacks.resource)
        return;
    CString utf8URL = url.utf8();
    m_callbacks.resource(m_callbacks.context, cString(utf8URL), 1, nullptr);
}

void WebView::resourceEnded(const String& url, const String& error)
{
    if (!m_callbacks.resource)
        return;
    CString utf8URL = url.utf8();
    CString utf8Error = error.utf8();
    m_callbacks.resource(m_callbacks.context, cString(utf8URL), 0, error.isNull() ? nullptr : cString(utf8Error));
}

} // namespace GFOpenBrowser
