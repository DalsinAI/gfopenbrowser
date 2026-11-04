/*
 * GFOpenBrowser: WebCore's chrome client (see AmigaChromeClient.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "config.h"
#include "AmigaChromeClient.h"

#include "AmigaWebView.h"

#include <WebCore/DocumentPage.h>
#include <WebCore/DocumentView.h>
#include <WebCore/FrameDestructionObserverInlines.h>
#include <WebCore/NodeDocument.h>
#include <WebCore/ChromeClient.h>
#include <WebCore/ColorChooser.h>
#include <WebCore/DataListSuggestionPicker.h>
#include <WebCore/DateTimeChooser.h>
#include <WebCore/FileChooser.h>
#include <WebCore/FloatRect.h>
#include <WebCore/ElementAncestorIteratorInlines.h>
#include <WebCore/HTMLFormControlElement.h>
#include <WebCore/HTMLFormElement.h>
#include <WebCore/HitTestResult.h>
#include <WebCore/Icon.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/NavigationAction.h>
#include <WebCore/Page.h>
#include <WebCore/PopupMenu.h>
#include <WebCore/PopupMenuClient.h>
#include <WebCore/SearchPopupMenu.h>
#include <WebCore/TextIndicator.h>
#include <WebCore/WindowFeatures.h>
#include <wtf/TZoneMallocInlines.h>

namespace GFOpenBrowser {

using namespace WebCore;

// <select> lists open nothing yet; the window will show them later.
class AmigaPopupMenu final : public PopupMenu {
public:
    void show(const IntRect&, LocalFrameView&, int) final { }
    void hide() final { }
    void updateFromElement() final { }
    void disconnectClient() final { }
};

class AmigaSearchPopupMenu final : public SearchPopupMenu {
public:
    AmigaSearchPopupMenu()
        : m_popup(adoptRef(*new AmigaPopupMenu))
    {
    }
    PopupMenu* popupMenu() final { return m_popup.ptr(); }
    void saveRecentSearches(const AtomString&, const Vector<RecentSearch>&) final { }
    void loadRecentSearches(const AtomString&, Vector<RecentSearch>&) final { }
    bool enabled() final { return false; }

private:
    Ref<AmigaPopupMenu> m_popup;
};

class AmigaChromeClient final : public ChromeClient {
    WTF_MAKE_TZONE_ALLOCATED(AmigaChromeClient);
public:
    explicit AmigaChromeClient(WebView& view)
        : m_view(view)
    {
    }

private:
    void chromeDestroyed() final { }

    void setWindowRect(const FloatRect&) final { }
    FloatRect windowRect() const final { return FloatRect(FloatPoint(), FloatSize(m_view.size())); }
    FloatRect pageRect() const final { return windowRect(); }

    void focus() final { }
    void unfocus() final { }
    bool canTakeFocus(FocusDirection) const final { return true; }
    void takeFocus(FocusDirection) final { }
    // Someone started on a form: the loads Tata's defer rules held (a
    // CAPTCHA's script, which the form needs before it is sent) go now.
    void focusedElementChanged(Element* element, LocalFrame*, FocusOptions, BroadcastFocusedElement) final
    {
        if (element && (is<HTMLFormControlElement>(*element) || element->isTextField() || ancestorsOfType<HTMLFormElement>(*element).first()))
            m_view.releaseDeferredLoads();
    }
    void focusedFrameChanged(Frame*) final { }

    // One window: links that would open another load in this one.
    RefPtr<Page> createWindow(LocalFrame&, const String&, const WindowFeatures&, const NavigationAction&) final { return nullptr; }
    void show() final { }

    bool canRunModal() const final { return false; }
    void runModal() final { }
    bool isPopup() const final { return false; }
    void setResizable(bool) final { }

    void addMessageToConsole(JSC::MessageSource, JSC::MessageLevel, const String& message, unsigned lineNumber, unsigned, const String& sourceID) final
    {
        m_view.consoleMessage(message, lineNumber, sourceID);
    }

    bool canRunBeforeUnloadConfirmPanel() final { return true; }
    bool runBeforeUnloadConfirmPanel(String&& message, LocalFrame&) final { return m_view.runConfirm(message); }

    void closeWindow() final { }
    void rootFrameAdded(const LocalFrame&) final { }
    void rootFrameRemoved(const LocalFrame&) final { }

    void runJavaScriptAlert(LocalFrame&, const String& message) final { m_view.runAlert(message); }
    bool runJavaScriptConfirm(LocalFrame&, const String& message) final { return m_view.runConfirm(message); }
    bool runJavaScriptPrompt(LocalFrame&, const String& message, const String& defaultValue, String& result) final { return m_view.runPrompt(message, defaultValue, result); }

    RefPtr<PopupMenu> createPopupMenu(PopupMenuClient&) const final { return adoptRef(*new AmigaPopupMenu); }
    RefPtr<SearchPopupMenu> createSearchPopupMenu(PopupMenuClient&) const final { return adoptRef(*new AmigaSearchPopupMenu); }

    KeyboardUIMode keyboardUIMode() final { return KeyboardAccessTabsToLinks; }

    bool hasAccessoryMousePointingDevice() const final { return true; }
    bool hoverSupportedByPrimaryPointingDevice() const final { return true; }
    bool hoverSupportedByAnyAvailablePointingDevice() const final { return true; }
    std::optional<PointerCharacteristics> pointerCharacteristicsOfPrimaryPointingDevice() const final { return PointerCharacteristics::Fine; }
    OptionSet<PointerCharacteristics> pointerCharacteristicsOfAllAvailablePointingDevices() const final { return PointerCharacteristics::Fine; }

    void invalidateRootView(const IntRect&) final { }
    void invalidateContentsAndRootView(const IntRect& rect) final { m_view.invalidate(rect); }
    void invalidateContentsForSlowScroll(const IntRect& rect) final { m_view.invalidate(rect); }
    void scroll(const IntSize&, const IntRect&, const IntRect& clipRect) final { m_view.invalidate(clipRect); }

    IntPoint screenToRootView(const IntPoint& p) const final { return p; }
    IntPoint rootViewToScreen(const IntPoint& p) const final { return p; }
    IntRect rootViewToScreen(const IntRect& r) const final { return r; }
    IntPoint accessibilityScreenToRootView(const IntPoint& p) const final { return p; }
    IntRect rootViewToAccessibilityScreen(const IntRect& r) const final { return r; }

    void didFinishLoadingImageForElement(HTMLImageElement&) final { }

    PlatformPageClient platformPageClient() const final { return 0; }
    void contentsSizeChanged(LocalFrame&, const IntSize& size) const final { m_view.contentsSizeChanged(size); }
    void intrinsicContentsSizeChanged(const IntSize&) const final { }

    void mouseDidMoveOverElement(const HitTestResult& result, OptionSet<PlatformEventModifier>, const String& toolTip, TextDirection) final
    {
        URL url = result.absoluteLinkURL();
        String status = !url.isEmpty() ? url.string() : toolTip;
        if (status != m_lastStatus) {
            m_lastStatus = status;
            m_view.setStatusText(status);
        }
    }

    void print(LocalFrame&, const StringWithDirection&) final { }
    void exceededDatabaseQuota(LocalFrame&, const String&, DatabaseDetails) final { }

    RefPtr<ColorChooser> createColorChooser(ColorChooserClient&, const Color&) final { return nullptr; }
    RefPtr<DataListSuggestionPicker> createDataListSuggestionPicker(DataListSuggestionsClient&) final { return nullptr; }
    bool canShowDataListSuggestionLabels() const final { return false; }
    RefPtr<DateTimeChooser> createDateTimeChooser(DateTimeChooserClient&) final { return nullptr; }

    void setTextIndicator(RefPtr<TextIndicator>&&) const final { }
    void updateTextIndicator(RefPtr<TextIndicator>&&) const final { }

    DisplayRefreshMonitorFactory* displayRefreshMonitorFactory() const final { return nullptr; }

    void runOpenPanel(LocalFrame&, FileChooser&) final { }
    void showShareSheet(ShareDataWithParsedURL&&, CompletionHandler<void(bool)>&& completionHandler) final { completionHandler(false); }
    void loadIconForFiles(const Vector<String>&, FileIconLoader&) final { }

    void elementDidFocus(Element&, const FocusOptions&) final { }
    void elementDidBlur(Element&) final { }

    void setCursor(const Cursor&) final { }
    void setCursorHiddenUntilMouseMoves(bool) final { }

    void scrollContainingScrollViewsToRevealRect(const IntRect&) const final { }
    void scrollMainFrameToRevealRect(const IntRect&) const final { }

    // No accelerated compositing: the page paints into the window's buffer.
    void attachRootGraphicsLayer(LocalFrame&, GraphicsLayer*) final { }
    void attachViewOverlayGraphicsLayer(GraphicsLayer*) final { }
    void setNeedsOneShotDrawingSynchronization() final { }
    void triggerRenderingUpdate() final { m_view.scheduleRenderingUpdate(); }

    void postAccessibilityNotification(AccessibilityObject&, AXNotification) final { }
    void postAccessibilityNodeTextChangeNotification(AccessibilityObject*, AXTextChange, unsigned, const String&) final { }
    void postAccessibilityFrameLoadingEventNotification(AccessibilityObject*, AXLoadingEvent) final { }

    void wheelEventHandlersChanged(bool) final { }

    void didAssociateFormControls(const Vector<Ref<Element>>&, LocalFrame&) final { }
    bool shouldNotifyOnFormChanges() final { return false; }

    RefPtr<Icon> createIconForFiles(const Vector<String>&) final { return nullptr; }

    WebView& m_view;
    String m_lastStatus;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(AmigaChromeClient);

UniqueRef<ChromeClient> createChromeClient(WebView& view)
{
    return makeUniqueRef<AmigaChromeClient>(view);
}

} // namespace GFOpenBrowser
