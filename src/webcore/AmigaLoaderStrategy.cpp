/*
 * OpenBrowser: WebCore's loader strategy with the network. Subresources are
 * queued per host and started through ResourceHandle (openweb.fetch), at
 * most a few at a time for each host; local files and data: addresses start
 * at once.
 *
 * Adapted from WebKitLegacy's WebResourceLoadScheduler, which is under the
 * GNU Library General Public License, version 2 or later:
 *
 *  Copyright (C) 1998 Lars Knoll (knoll@mpi-hd.mpg.de)
 *  Copyright (C) 2001 Dirk Mueller (mueller@kde.org)
 *  Copyright (C) 2002 Waldo Bastian (bastian@kde.org)
 *  Copyright (C) 2004-2025 Apple Inc. All rights reserved.
 *  Copyright (C) 2010 Google Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 * The Amiga changes are by the AmigaChrome Team (2026), under the same licence.
 */
#include "config.h"
#include "AmigaLoaderStrategy.h"

#include <WebCore/ArchiveResource.h>
#include <WebCore/CachedResource.h>
#include <WebCore/Document.h>
#include <WebCore/DocumentLoader.h>
#include <WebCore/FetchOptions.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LoaderStrategy.h>
#include <WebCore/LocalFrameInlines.h>
#include <WebCore/NetworkStateNotifier.h>
#include <WebCore/NetworkingContext.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceHandle.h>
#include <WebCore/ResourceLoader.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/SecurityOrigin.h>
#include <WebCore/SubresourceLoader.h>
#include <WebCore/Timer.h>
#include <array>
#include <wtf/Deque.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/StringHash.h>

namespace OpenBrowser {

using namespace WebCore;

static constexpr auto errorDomain = "OpenBrowser"_s;

// Requests handed to the network at once for one host, as desktop browsers
// do. openweb.fetch keeps up to six connections per host (its native
// transport also speaks HTTP/2, where the requests are streams on one
// connection).
static constexpr unsigned maxRequestsInFlightPerHost = 6;
static constexpr unsigned maxRequestsInFlightForNonHTTPProtocols = 20;

class AmigaLoaderStrategy final : public LoaderStrategy {
public:
    AmigaLoaderStrategy()
        : m_nonHTTPProtocolHost(makeUnique<HostInformation>(String(), maxRequestsInFlightForNonHTTPProtocols))
        , m_requestTimer([this] { servePendingRequests(); })
    {
    }

private:
    class HostInformation {
        WTF_MAKE_NONCOPYABLE(HostInformation);
        WTF_MAKE_TZONE_ALLOCATED(HostInformation);
    public:
        HostInformation(const String& name, unsigned maxRequestsInFlight)
            : m_name(name)
            , m_maxRequestsInFlight(maxRequestsInFlight)
        {
        }

        const String& name() const { return m_name; }

        void schedule(ResourceLoader* loader, ResourceLoadPriority priority)
        {
            m_requestsPending[priorityToIndex(priority)].append(loader);
        }

        void addLoadInProgress(ResourceLoader* loader) { m_requestsLoading.add(loader); }

        void remove(ResourceLoader* loader)
        {
            if (m_requestsLoading.remove(loader))
                return;
            for (auto& queue : m_requestsPending) {
                for (auto it = queue.begin(), end = queue.end(); it != end; ++it) {
                    if (*it == loader) {
                        queue.remove(it);
                        return;
                    }
                }
            }
        }

        bool hasRequests() const
        {
            if (!m_requestsLoading.isEmpty())
                return true;
            for (auto& queue : m_requestsPending) {
                if (!queue.isEmpty())
                    return true;
            }
            return false;
        }

        bool limitRequests(ResourceLoadPriority priority) const
        {
            if (priority == ResourceLoadPriority::VeryLow && !m_requestsLoading.isEmpty())
                return true;
            return m_requestsLoading.size() >= m_maxRequestsInFlight;
        }

        Deque<RefPtr<ResourceLoader>>& requestsPending(ResourceLoadPriority priority) { return m_requestsPending[priorityToIndex(priority)]; }

    private:
        static unsigned priorityToIndex(ResourceLoadPriority priority)
        {
            switch (priority) {
            case ResourceLoadPriority::VeryLow: return 0;
            case ResourceLoadPriority::Low: return 1;
            case ResourceLoadPriority::Medium: return 2;
            case ResourceLoadPriority::High: return 3;
            case ResourceLoadPriority::VeryHigh: return 4;
            }
            return 0;
        }

        std::array<Deque<RefPtr<ResourceLoader>>, resourceLoadPriorityCount> m_requestsPending;
        HashSet<RefPtr<ResourceLoader>> m_requestsLoading;
        const String m_name;
        const unsigned m_maxRequestsInFlight;
    };

    HostInformation* hostForURL(const URL& url, bool createIfNotFound = false)
    {
        if (!url.protocolIsInHTTPFamily())
            return m_nonHTTPProtocolHost.get();
        String hostName = url.host().toString();
        auto it = m_hosts.find(hostName);
        if (it != m_hosts.end())
            return it->value.get();
        if (!createIfNotFound)
            return nullptr;
        auto host = makeUnique<HostInformation>(hostName, maxRequestsInFlightPerHost);
        auto* result = host.get();
        m_hosts.add(hostName, WTF::move(host));
        return result;
    }

    void loadResource(LocalFrame& frame, CachedResource& resource, ResourceRequest&& request, const ResourceLoaderOptions& options, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completionHandler) final
    {
        SubresourceLoader::create(frame, resource, WTF::move(request), options, [this, completionHandler = WTF::move(completionHandler)] (RefPtr<SubresourceLoader>&& loader) mutable {
            if (loader)
                scheduleLoad(loader.get());
            completionHandler(WTF::move(loader));
        });
    }

    void loadResourceSynchronously(FrameLoader& frameLoader, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions& options, const HTTPHeaderMap&, ResourceError& error, ResourceResponse& response, Vector<uint8_t>& data) final
    {
        RefPtr document = frameLoader.frame().document();
        RefPtr sourceOrigin = document ? &document->securityOrigin() : nullptr;
        RefPtr context = frameLoader.networkingContext();
        ResourceHandle::loadResourceSynchronously(context.get(), request, options.credentials == FetchOptions::Credentials::Omit ? StoredCredentialsPolicy::DoNotUse : StoredCredentialsPolicy::Use, sourceOrigin.get(), error, response, data);
    }

    void pageLoadCompleted(Page&) final { }
    void browsingContextRemoved(LocalFrame&) final { }

    void scheduleLoad(ResourceLoader* loader)
    {
        if (RefPtr documentLoader = loader->documentLoader(); documentLoader && documentLoader->archiveResourceForURL(loader->request().url())) {
            loader->start();
            return;
        }
        auto* host = hostForURL(loader->url(), true);
        auto priority = loader->request().priority();
        bool hadRequests = host->hasRequests();
        host->schedule(loader, priority);
        if (priority > ResourceLoadPriority::Low || !loader->url().protocolIsInHTTPFamily() || (priority == ResourceLoadPriority::Low && !hadRequests)) {
            // Important resources go at once.
            servePendingRequests(*host, priority);
            return;
        }
        // Later ones wait a moment, so early low-priority requests do not
        // go before high-priority ones that follow them.
        scheduleServePendingRequests();
    }

    void remove(ResourceLoader* loader) final
    {
        if (auto* host = hostForURL(loader->url()))
            host->remove(loader);
        scheduleServePendingRequests();
    }

    void setDefersLoading(ResourceLoader& loader, bool defers) final
    {
        if (!defers && !loader.deferredRequest().isNull()) {
            loader.setRequest(loader.takeDeferredRequest());
            loader.start();
        }
    }

    void crossOriginRedirectReceived(ResourceLoader* loader, const URL& redirectURL) final
    {
        auto* oldHost = hostForURL(loader->url());
        if (!oldHost)
            return;
        auto* newHost = hostForURL(redirectURL, true);
        if (oldHost->name() == newHost->name())
            return;
        newHost->addLoadInProgress(loader);
        oldHost->remove(loader);
    }

    void servePendingRequests(ResourceLoadPriority minimumPriority = ResourceLoadPriority::VeryLow) final
    {
        if (m_suspendPendingRequestsCount)
            return;
        m_requestTimer.stop();
        servePendingRequests(*m_nonHTTPProtocolHost, minimumPriority);
        Vector<String> idle;
        for (auto& entry : m_hosts) {
            if (entry.value->hasRequests())
                servePendingRequests(*entry.value, minimumPriority);
            else
                idle.append(entry.key);
        }
        for (auto& name : idle)
            m_hosts.remove(name);
    }

    void servePendingRequests(HostInformation& host, ResourceLoadPriority minimumPriority)
    {
        auto priority = ResourceLoadPriority::Highest;
        while (true) {
            auto& requestsPending = host.requestsPending(priority);
            while (!requestsPending.isEmpty()) {
                RefPtr loader = requestsPending.first();
                RefPtr document = loader->frameLoader() ? loader->frameLoader()->frame().document() : nullptr;
                bool shouldLimitRequests = !host.name().isNull() || (document && (document->parsing() || !document->haveStylesheetsLoaded()));
                if (shouldLimitRequests && host.limitRequests(priority))
                    return;
                requestsPending.removeFirst();
                host.addLoadInProgress(loader.get());
                loader->start();
            }
            if (priority == minimumPriority)
                return;
            --priority;
        }
    }

    void suspendPendingRequests() final { ++m_suspendPendingRequestsCount; }

    void resumePendingRequests() final
    {
        if (m_suspendPendingRequestsCount && --m_suspendPendingRequestsCount)
            return;
        scheduleServePendingRequests();
    }

    void scheduleServePendingRequests()
    {
        if (!m_requestTimer.isActive())
            m_requestTimer.startOneShot(0_s);
    }

    void preconnectTo(FrameLoader&, ResourceRequest&&, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& completionHandler) final
    {
        if (completionHandler)
            completionHandler(ResourceError(errorDomain, 1, { }, "no preconnecting"_s));
    }

    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }

    bool isOnLine() const final { return NetworkStateNotifier::singleton().onLine(); }
    void addOnlineStateChangeListener(Function<void(bool)>&& listener) final { NetworkStateNotifier::singleton().addListener(WTF::move(listener)); }

    void isResourceLoadFinished(CachedResource& resource, CompletionHandler<void(bool)>&& callback) final
    {
        if (!resource.loader()) {
            callback(true);
            return;
        }
        callback(!hostForURL(resource.loader()->url()));
    }

    ResourceError cancelledError(const ResourceRequest& request) const final { return error(request.url(), 2, "cancelled"_s, ResourceError::Type::Cancellation); }
    ResourceError blockedError(const ResourceRequest& request) const final { return error(request.url(), 3, "blocked"_s); }
    bool isBlockedError(const ResourceError& error) const final { return error.domain() == errorDomain && error.errorCode() == 3; }
    ResourceError blockedByContentBlockerError(const ResourceRequest& request) const final { return error(request.url(), 4, "blocked by a content blocker"_s); }
    ResourceError cannotShowURLError(const ResourceRequest& request) const final { return error(request.url(), 5, "OpenBrowser cannot show this address"_s); }
    ResourceError interruptedForPolicyChangeError(const ResourceRequest& request) const final { return error(request.url(), 6, "interrupted"_s, ResourceError::Type::Cancellation); }
#if ENABLE(CONTENT_FILTERING)
    ResourceError blockedByContentFilterError(const ResourceRequest& request) const final { return error(request.url(), 7, "blocked by a content filter"_s); }
#endif
    ResourceError cannotShowMIMETypeError(const ResourceResponse& response) const final { return error(response.url(), 8, "OpenBrowser cannot show this type of file"_s); }
    ResourceError fileDoesNotExistError(const ResourceResponse& response) const final { return error(response.url(), 9, "file not found"_s); }
    ResourceError httpsUpgradeRedirectLoopError(const ResourceRequest& request) const final { return error(request.url(), 10, "HTTPS upgrade loop"_s); }
    ResourceError httpNavigationWithHTTPSOnlyError(const ResourceRequest& request) const final { return error(request.url(), 11, "HTTPS only"_s); }
    bool isHttpNavigationWithHTTPSOnlyError(const ResourceError& error) const final { return error.domain() == errorDomain && error.errorCode() == 11; }
    ResourceError pluginWillHandleLoadError(const ResourceResponse& response) const final { return error(response.url(), 12, "plug-in"_s); }

    static ResourceError error(const URL& url, int code, ASCIILiteral description, ResourceError::Type type = ResourceError::Type::General)
    {
        return ResourceError(errorDomain, code, url, description, type);
    }

    HashMap<String, std::unique_ptr<HostInformation>> m_hosts;
    std::unique_ptr<HostInformation> m_nonHTTPProtocolHost;
    Timer m_requestTimer;
    unsigned m_suspendPendingRequestsCount { 0 };
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(AmigaLoaderStrategy::HostInformation);

LoaderStrategy* networkLoaderStrategy()
{
    static NeverDestroyed<AmigaLoaderStrategy> strategy;
    return &strategy.get();
}

} // namespace OpenBrowser
