/*
 * GFOpenBrowser: WebCore's frame loader client, one per frame. It makes the
 * frame's view when a page commits, says yes to navigations and to the
 * types WebCore can show, makes subframes for <iframe>, and tells the main
 * frame's window about titles, addresses and progress.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "config.h"
#include "AmigaFrameLoaderClient.h"

#include "AmigaWebView.h"
#include "ob_tata.h"

#include <WebCore/DocumentPage.h>
#include <WebCore/DocumentView.h>
#include <WebCore/FrameDestructionObserverInlines.h>
#include <WebCore/NodeDocument.h>
#include <WebCore/AuthenticationChallenge.h>
#include <WebCore/AuthenticationClient.h>
#include <WebCore/Color.h>
#include <WebCore/DocumentLoader.h>
#include <WebCore/FormState.h>
#include <WebCore/FrameLoadRequest.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/FrameNetworkingContext.h>
#include <WebCore/FrameTreeSyncData.h>
#include <WebCore/HTMLFrameOwnerElement.h>
#include <WebCore/HistoryController.h>
#include <WebCore/HistoryItem.h>
#include <WebCore/HitTestResult.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h>
#include <WebCore/LocalFrameLoaderClient.h>
#include <WebCore/LocalFrameView.h>
#include <WebCore/MIMETypeRegistry.h>
#include <WebCore/NavigationAction.h>
#include <WebCore/Page.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/ScrollTypes.h>
#include <WebCore/SharedBuffer.h>
#include <WebCore/SubstituteData.h>
#include <WebCore/UserAgent.h>
#include <WebCore/Widget.h>
#include <stdio.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RunLoop.h>

namespace GFOpenBrowser {

using namespace WebCore;

class AmigaNetworkingContext final : public FrameNetworkingContext {
public:
    static Ref<AmigaNetworkingContext> create(LocalFrame& frame) { return adoptRef(*new AmigaNetworkingContext(frame)); }

private:
    explicit AmigaNetworkingContext(LocalFrame& frame)
        : FrameNetworkingContext(&frame)
    {
    }
    // Cookies live in GFOpenBrowser's own jar (AmigaCookieJar), not here.
    CookieStorageSession* storageSession() const final { return nullptr; }
};

// Tata's ad and tracker rules (ob_tata.h): a request they block is refused
// here, so the view counts it as blocked; ResourceHandle checks every load
// again (WebKit patch 0098), pings and workers' too.
static bool isBlockedByTata(const ResourceRequest& request)
{
    const URL& url = request.url();
    if (!url.protocolIsInHTTPFamily())
        return false;
    auto host = url.host().toString().convertToASCIILowercase().utf8();
    auto path = url.path().toString().utf8();
    auto site = request.firstPartyForCookies().host().toString().convertToASCIILowercase().utf8();
    return ob_tata_blocked(reinterpret_cast<const char*>(host.data()), reinterpret_cast<const char*>(path.data()),
        reinterpret_cast<const char*>(site.data()));
}

// Lite: the standard user agent with "Mobile", so sites send the pages they
// make for phones, which are smaller and carry less script.
static const String& liteUserAgent()
{
    static NeverDestroyed<String> userAgent { "Mozilla/5.0 (Amiga; AmigaOS 3.2; m68k; Mobile) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile Safari/605.1.15"_s };
    return userAgent;
}

class AmigaFrameLoaderClient final : public LocalFrameLoaderClient {
public:
    AmigaFrameLoaderClient(FrameLoader& loader, WebView& view)
        : LocalFrameLoaderClient(loader)
        , m_frameLoader(loader)
        , m_view(view)
    {
    }

private:
    LocalFrame& frame() const { return m_frameLoader->frame(); }
    bool isMainFrame() const { return frame().isMainFrame(); }

    Ref<DocumentLoader> createDocumentLoader(ResourceRequest&& request, SubstituteData&& substituteData, ResourceRequest&& originalRequest) final
    {
        return DocumentLoader::create(WTF::move(request), WTF::move(substituteData), WTF::move(originalRequest));
    }
    Ref<DocumentLoader> createDocumentLoader(ResourceRequest&& request, SubstituteData&& substituteData) final
    {
        return DocumentLoader::create(WTF::move(request), WTF::move(substituteData), { });
    }

    bool hasWebView() const final { return true; }
    void makeRepresentation(DocumentLoader*) final { }
    void forceLayoutForNonHTML() final { }
    void setCopiesOnScroll() final { }
    void detachedFromParent2() final { }
    void detachedFromParent3() final { }

    void convertMainResourceLoadToDownload(DocumentLoader*, const ResourceRequest&, const ResourceResponse&) final { }

    void assignIdentifierToInitialRequest(ResourceLoaderIdentifier, DocumentLoader*, const ResourceRequest&) final { }
    bool shouldUseCredentialStorage(DocumentLoader*, ResourceLoaderIdentifier) final { return true; }
    void dispatchWillSendRequest(DocumentLoader*, ResourceLoaderIdentifier identifier, ResourceRequest& request, const ResourceResponse&) final
    {
        if (isBlockedByTata(request)) {
            // WebCore cancels a load whose request comes back empty.
            m_view.resourceEnded(request.url().string(), "blocked"_s);
            request = { };
            return;
        }
        // Also called again for each redirect, with the new address.
        auto url = request.url().string();
        m_requestURLs.set(identifier, url);
        m_view.resourceStarted(url);
    }
    void dispatchDidReceiveAuthenticationChallenge(DocumentLoader*, ResourceLoaderIdentifier, const AuthenticationChallenge& challenge) final
    {
        // No HTTP log-in panel yet: carry on without credentials.
        if (RefPtr client = challenge.authenticationClient())
            client->receivedRequestToContinueWithoutCredential(challenge);
    }

    void dispatchDidReceiveResponse(DocumentLoader*, ResourceLoaderIdentifier, const ResourceResponse&) final { }
    // Bytes as they arrive, counted for obcore-view's benchmark line.
    void dispatchDidReceiveContentLength(DocumentLoader*, ResourceLoaderIdentifier, int length) final { m_view.addBytesReceived(length); }
    void dispatchDidFinishLoading(DocumentLoader*, ResourceLoaderIdentifier identifier) final
    {
        m_view.resourceEnded(m_requestURLs.take(identifier), { });
    }
    void dispatchDidFailLoading(DocumentLoader*, ResourceLoaderIdentifier identifier, const ResourceError& error) final
    {
        m_view.resourceEnded(m_requestURLs.take(identifier), error.localizedDescription().isEmpty() ? "failed"_s : error.localizedDescription());
    }
    bool dispatchDidLoadResourceFromMemoryCache(DocumentLoader*, const ResourceRequest&, const ResourceResponse&, int) final { return false; }

    void dispatchDidDispatchOnloadEvents() final { }
    void dispatchDidReceiveServerRedirectForProvisionalLoad() final { }
    void dispatchDidCancelClientRedirect() final { }
    void dispatchWillPerformClientRedirect(const URL&, double, WallTime, LockBackForwardList) final { }
    void dispatchDidChangeLocationWithinPage() final { notifyURL(); }
    void dispatchDidPushStateWithinPage() final { notifyURL(); }
    void dispatchDidReplaceStateWithinPage() final { notifyURL(); }
    void dispatchDidPopStateWithinPage() final { notifyURL(); }
    void dispatchWillClose() final { }

    void dispatchDidStartProvisionalLoad() final
    {
        if (isMainFrame())
            m_view.didStartLoad();
    }
    void dispatchDidReceiveTitle(const StringWithDirection& title) final
    {
        if (isMainFrame())
            m_view.setTitle(title.string);
    }
    void dispatchDidCommitLoad(const std::optional<BackForwardCacheCommitData>&) final { notifyURL(); }
    void dispatchDidFailProvisionalLoad(const ResourceError& error, WillContinueLoading willContinue, WillInternallyHandleFailure) final
    {
        if (isMainFrame() && willContinue == WillContinueLoading::No)
            m_view.didFailLoad(error);
    }
    void dispatchDidFailLoad(const ResourceError& error) final
    {
        if (isMainFrame())
            m_view.didFailLoad(error);
    }
    void dispatchDidFinishDocumentLoad() final { }
    void dispatchDidFinishLoad() final
    {
        if (isMainFrame())
            m_view.didFinishLoad();
    }
    void dispatchDidReachLayoutMilestone(OptionSet<LayoutMilestone>) final { }
    void dispatchDidReachVisuallyNonEmptyState() final { }

    LocalFrame* dispatchCreatePage(const NavigationAction&, NewFrameOpenerPolicy, const String&) final { return nullptr; }
    void dispatchShow() final { }

    void dispatchDecidePolicyForResponse(const ResourceResponse& response, const ResourceRequest&, const String&, FramePolicyFunction&& function) final
    {
        // Show what WebCore can show; downloads come later.
        if (canShowMIMEType(response.mimeType()) || response.mimeType().isEmpty())
            function(PolicyAction::Use);
        else
            function(PolicyAction::Ignore);
    }

    void dispatchDecidePolicyForNewWindowAction(const NavigationAction&, const ResourceRequest& request, FormState*, const String&, std::optional<HitTestResult>&&, FramePolicyFunction&& function) final
    {
        // One window: a link meant for a new window opens in this one.
        function(PolicyAction::Ignore);
        URL url = request.url();
        RunLoop::mainSingleton().dispatch([view = &m_view, url = url.isolatedCopy()] {
            view->load(url.string());
        });
    }

    void dispatchDecidePolicyForNavigationAction(const NavigationAction&, const ResourceRequest&, const ResourceResponse&, FormState*, const String&, std::optional<NavigationIdentifier>, std::optional<HitTestResult>&&, bool, NavigationUpgradeToHTTPSBehavior, SandboxFlags, PolicyDecisionMode, FramePolicyFunction&& function) final
    {
        function(PolicyAction::Use);
    }

    void updateSandboxFlags(SandboxFlags) final { }
    void updateOpener(std::optional<FrameIdentifier>) final { }
    void setPrinting(bool, FloatSize, FloatSize, float, AdjustViewSize) final { }
    void cancelPolicyCheck() final { }
    void dispatchUnableToImplementPolicy(const ResourceError&) final { }

    void dispatchWillSendSubmitEvent(Ref<FormState>&&) final { }
    void dispatchWillSubmitForm(FormState&, URL&&, String&&, CompletionHandler<void()>&& completionHandler) final { completionHandler(); }

    void revertToProvisionalState(DocumentLoader*) final { }
    void setMainDocumentError(DocumentLoader*, const ResourceError&) final { }
    void setMainFrameDocumentReady(bool) final { }

    void startDownload(const ResourceRequest&, const String&, FromDownloadAttribute) final { }

    void willChangeTitle(DocumentLoader*) final { }
    void didChangeTitle(DocumentLoader*) final { }
    void willReplaceMultipartContent() final { }
    void didReplaceMultipartContent() final { }

    // The page's bytes go to the document as they arrive.
    void committedLoad(DocumentLoader* loader, const SharedBuffer& data) final { loader->commitData(data); }
    void finishedLoading(DocumentLoader*) final { }

    void loadStorageAccessQuirksIfNeeded() final { }

    bool shouldFallBack(const ResourceError& error) const final { return !error.isCancellation(); }

    bool canHandleRequest(const ResourceRequest&) const final { return true; }
    bool canShowMIMEType(const String& type) const final { return MIMETypeRegistry::canShowMIMEType(type); }
    bool canShowMIMETypeAsHTML(const String&) const final { return false; }
    bool representationExistsForURLScheme(StringView) const final { return false; }
    String generatedMIMETypeForURLScheme(StringView) const final { return emptyString(); }

    void frameLoadCompleted() final { }
    void restoreViewState() final { frame().loader().history().restoreScrollPositionAndViewState(); }
    void provisionalLoadStarted() final { }
    void didFinishLoad() final { }
    void prepareForDataSourceReplacement() final { }
    void updateCachedDocumentLoader(DocumentLoader&) final { }
    void setTitle(const StringWithDirection&, const URL&) final { }

    String userAgent(const URL&) const final { return m_view.liteMode() ? liteUserAgent() : standardUserAgent(); }

    void savePlatformDataToCachedFrame(CachedFrame*) final { }
    void transitionToCommittedFromCachedFrame(CachedFrame*) final { }

    void transitionToCommittedForNewPage(InitializingIframe) final
    {
        Ref frame = this->frame();
        IntSize size = frame->isMainFrame() ? m_view.size() : IntSize();
        frame->createView(size, Color::white, { }, false, ScrollbarMode::Auto, false, ScrollbarMode::Auto, false);
    }

    void didRestoreFromBackForwardCache() final { }

    void updateGlobalHistory() final { }
    void updateGlobalHistoryRedirectLinks() final { }
    ShouldGoToHistoryItem shouldGoToHistoryItem(HistoryItem&, IsSameDocumentNavigation) const final { return ShouldGoToHistoryItem::Yes; }
    bool supportsAsyncShouldGoToHistoryItem() const final { return false; }
    void shouldGoToHistoryItemAsync(HistoryItem&, CompletionHandler<void(ShouldGoToHistoryItem)>&& completionHandler) const final { completionHandler(ShouldGoToHistoryItem::Yes); }
    void dispatchGoToBackForwardItemAtIndex(int) final { }
    void dispatchEnqueueHistoryTraversalDelta(int) final { }

    void saveViewStateToItem(HistoryItem&) final { }
    bool canCachePage() const final { return false; }

    RefPtr<LocalFrame> createFrame(const AtomString& name, HTMLFrameOwnerElement& ownerElement) final
    {
        RefPtr ownerFrame = ownerElement.document().frame();
        if (!ownerFrame)
            return nullptr;
        RefPtr page = ownerFrame->page();
        if (!page)
            return nullptr;

        auto sandboxFlags = ownerElement.sandboxFlags();
        sandboxFlags.add(ownerFrame->effectiveSandboxFlags());
        auto referrerPolicy = ownerElement.referrerPolicy();
        if (RefPtr topDocument = page->localTopDocument(); referrerPolicy == ReferrerPolicy::EmptyString && topDocument)
            referrerPolicy = topDocument->referrerPolicy();

        WebView* view = &m_view;
        Ref subframe = LocalFrame::createSubframe(*page, [view](auto&, auto& frameLoader) {
            return makeUniqueRefWithoutRefCountedCheck<AmigaFrameLoaderClient>(frameLoader, *view);
        }, generateFrameIdentifier(), sandboxFlags, referrerPolicy, ownerElement, FrameTreeSyncData::create());
        subframe->tree().setSpecifiedName(name);
        subframe->init();

        // Running the frame's scripts may already have removed it again.
        if (!subframe->page())
            return nullptr;
        return subframe;
    }

    RefPtr<Widget> createPlugin(HTMLPlugInElement&, const URL&, const Vector<AtomString>&, const Vector<AtomString>&, const String&, bool) final { return nullptr; }

    ObjectContentType objectContentType(const URL& url, const String& mimeTypeIn) final
    {
        String mimeType = mimeTypeIn;
        if (mimeType.isEmpty())
            mimeType = MIMETypeRegistry::mimeTypeForPath(url.path().toString());
        if (mimeType.isEmpty())
            return ObjectContentType::Frame;
        if (MIMETypeRegistry::isSupportedImageMIMEType(mimeType))
            return ObjectContentType::Image;
        if (MIMETypeRegistry::isSupportedNonImageMIMEType(mimeType))
            return ObjectContentType::Frame;
        return ObjectContentType::None;
    }
    AtomString overrideMediaType() const final { return nullAtom(); }

    void redirectDataToPlugin(Widget&) final { }
    void dispatchDidClearWindowObjectInWorld(DOMWrapperWorld&) final { }

    Ref<FrameNetworkingContext> createNetworkingContext() final { return AmigaNetworkingContext::create(frame()); }

    bool isEmptyFrameLoaderClient() const final { return false; }
    void prefetchDNS(const String&) final { }
    void sendH2Ping(const URL& url, CompletionHandler<void(std::expected<Seconds, ResourceError>&&)>&& completionHandler) final
    {
        completionHandler(makeUnexpected(ResourceError(ResourceError::Type::General)));
    }

    bool hasFrameSpecificStorageAccess() final { return false; }
    void revokeFrameSpecificStorageAccess() final { }

    void dispatchLoadEventToOwnerElementInAnotherProcess() final { }

    RefPtr<HistoryItem> createHistoryItemTree(bool clipAtTarget, BackForwardItemIdentifier itemID) const final
    {
        Ref frame = this->frame();
        return frame->rootFrame().loader().history().createItemTree(frame, clipAtTarget, itemID);
    }

    void notifyURL()
    {
        if (!isMainFrame())
            return;
        if (RefPtr loader = frame().loader().documentLoader())
            m_view.didCommitLoad(loader->url().string());
    }

    WeakRef<FrameLoader> m_frameLoader;
    WebView& m_view;
    HashMap<ResourceLoaderIdentifier, String> m_requestURLs;
};

UniqueRef<LocalFrameLoaderClient> createFrameLoaderClient(FrameLoader& loader, WebView& view)
{
    return makeUniqueRefWithoutRefCountedCheck<AmigaFrameLoaderClient>(loader, view);
}

} // namespace GFOpenBrowser
