/*
 * OpenBrowser: WebCore's platform strategies for the headless programs.
 * The loader refuses every subresource load (the headless dump has no
 * network yet); the browser itself replaces this with a network loader.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "config.h"
#include "AmigaPlatformStrategies.h"

#include <wtf/TZoneMalloc.h>
#include <WebCore/BlobRegistry.h>
#include <WebCore/CachedResource.h>
#include <WebCore/LoaderStrategy.h>
#include <WebCore/PlatformStrategies.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/SubresourceLoader.h>
#include <wtf/CompletionHandler.h>
#include <wtf/NeverDestroyed.h>

namespace OpenBrowser {

using namespace WebCore;

static constexpr auto errorDomain = "OpenBrowser"_s;

class OfflineLoaderStrategy final : public LoaderStrategy {
public:
    void loadResource(LocalFrame&, CachedResource&, ResourceRequest&&, const ResourceLoaderOptions&, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completionHandler) final
    {
        completionHandler(nullptr);
    }
    void loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions&, const HTTPHeaderMap&, ResourceError& error, ResourceResponse&, Vector<uint8_t>&) final
    {
        error = cannotShowURLError(request);
    }
    void pageLoadCompleted(Page&) final { }
    void browsingContextRemoved(LocalFrame&) final { }
    void remove(ResourceLoader*) final { }
    void setDefersLoading(ResourceLoader&, bool) final { }
    void crossOriginRedirectReceived(ResourceLoader*, const URL&) final { }
    void servePendingRequests(ResourceLoadPriority) final { }
    void suspendPendingRequests() final { }
    void resumePendingRequests() final { }
    void preconnectTo(FrameLoader&, ResourceRequest&&, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& completionHandler) final
    {
        if (completionHandler)
            completionHandler(ResourceError(errorDomain, 1, { }, "offline"_s));
    }
    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }
    bool isOnLine() const final { return false; }
    void addOnlineStateChangeListener(Function<void(bool)>&&) final { }
    void isResourceLoadFinished(CachedResource&, CompletionHandler<void(bool)>&& callback) final { callback(true); }

    ResourceError cancelledError(const ResourceRequest& request) const final { return error(request.url(), 2, "cancelled"_s, ResourceError::Type::Cancellation); }
    ResourceError blockedError(const ResourceRequest& request) const final { return error(request.url(), 3, "blocked"_s); }
    bool isBlockedError(const ResourceError& error) const final { return error.domain() == errorDomain && error.errorCode() == 3; }
    ResourceError blockedByContentBlockerError(const ResourceRequest& request) const final { return error(request.url(), 4, "blocked by a content blocker"_s); }
    ResourceError cannotShowURLError(const ResourceRequest& request) const final { return error(request.url(), 5, "not loaded: no network in this program"_s); }
    ResourceError interruptedForPolicyChangeError(const ResourceRequest& request) const final { return error(request.url(), 6, "interrupted"_s, ResourceError::Type::Cancellation); }
#if ENABLE(CONTENT_FILTERING)
    ResourceError blockedByContentFilterError(const ResourceRequest& request) const final { return error(request.url(), 7, "blocked by a content filter"_s); }
#endif
    ResourceError cannotShowMIMETypeError(const ResourceResponse& response) const final { return error(response.url(), 8, "cannot show this type"_s); }
    ResourceError fileDoesNotExistError(const ResourceResponse& response) const final { return error(response.url(), 9, "file not found"_s); }
    ResourceError httpsUpgradeRedirectLoopError(const ResourceRequest& request) const final { return error(request.url(), 10, "HTTPS upgrade loop"_s); }
    ResourceError httpNavigationWithHTTPSOnlyError(const ResourceRequest& request) const final { return error(request.url(), 11, "HTTPS only"_s); }
    bool isHttpNavigationWithHTTPSOnlyError(const ResourceError& error) const final { return error.domain() == errorDomain && error.errorCode() == 11; }
    ResourceError pluginWillHandleLoadError(const ResourceResponse& response) const final { return error(response.url(), 12, "plug-in"_s); }

private:
    static ResourceError error(const URL& url, int code, ASCIILiteral description, ResourceError::Type type = ResourceError::Type::General)
    {
        return ResourceError(errorDomain, code, url, description, type);
    }
};

class EmptyBlobRegistry final : public BlobRegistry {
    void registerInternalFileBlobURL(const URL&, Ref<BlobDataFileReference>&&, const String&, const String&) final { }
    void registerInternalBlobURL(const URL&, Vector<BlobPart>&&, const String&) final { }
    void registerBlobURL(const URL&, const URL&, const PolicyContainer&, const std::optional<SecurityOriginData>&) final { }
    void registerInternalBlobURLOptionallyFileBacked(const URL&, const URL&, RefPtr<BlobDataFileReference>&&, const String&) final { }
    void registerInternalBlobURLForSlice(const URL&, const URL&, long long, long long, const String&) final { }
    void unregisterBlobURL(const URL&, const std::optional<SecurityOriginData>&) final { }
    String blobType(const URL&) final { return emptyString(); }
    unsigned long long blobSize(const URL&) final { return 0; }
    void writeBlobsToTemporaryFilesForIndexedDB(const Vector<String>&, CompletionHandler<void(Vector<String>&&)>&& completionHandler) final { completionHandler({ }); }
    void registerBlobURLHandle(const URL&, const std::optional<SecurityOriginData>&) final { }
    void unregisterBlobURLHandle(const URL&, const std::optional<SecurityOriginData>&) final { }
};

class AmigaPlatformStrategies final : public PlatformStrategies {
public:
    explicit AmigaPlatformStrategies(LoaderStrategy* loader)
        : m_loader(loader)
    {
    }

private:
    LoaderStrategy* createLoaderStrategy() final
    {
        static NeverDestroyed<OfflineLoaderStrategy> offlineStrategy;
        return m_loader ? m_loader : &offlineStrategy.get();
    }
    PasteboardStrategy* createPasteboardStrategy() final { return nullptr; }
    MediaStrategy* createMediaStrategy() final { return nullptr; }
    BlobRegistry* createBlobRegistry() final
    {
        static NeverDestroyed<EmptyBlobRegistry> blobRegistry;
        return &blobRegistry.get();
    }
#if ENABLE(DECLARATIVE_WEB_PUSH)
    PushStrategy* createPushStrategy() final { return nullptr; }
#endif

    LoaderStrategy* m_loader;
};

void initializePlatformStrategies(LoaderStrategy* loader)
{
    static NeverDestroyed<AmigaPlatformStrategies> platformStrategies(loader);
    setPlatformStrategies(&platformStrategies.get());
}

} // namespace OpenBrowser
