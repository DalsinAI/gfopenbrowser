/*
 * OpenBrowser: the HTTP disk cache joined to WebCore's network layer (see
 * AmigaHttpCache.h). For each GET request that HTTP and WebCore's cache
 * mode allow, a stored copy answers when it is still fresh; a stale one
 * gets If-None-Match and If-Modified-Since, and a 304 answer reuses it.
 * Responses are stored as their body arrives, without the headers that
 * must not come back (cookies, and the length and encoding the transport
 * undid).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "config.h"
#include "AmigaHttpCache.h"

#include "AmigaNetwork.h"
#include "ob_httpcache.h"

#include <WebCore/HTTPHeaderNames.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/SharedBuffer.h>
#include <time.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>

namespace OpenBrowser {

using namespace WebCore;

static ob_httpcache* s_cache;

// A header's value for the C side: null when the header is missing.
static const char* headerOrNull(const UTF8CString& value, bool present)
{
    return present ? value.legacyCStringPointer() : nullptr;
}

class HttpCacheRequest final : public AmigaHttpCacheRequest {
    WTF_MAKE_TZONE_ALLOCATED(HttpCacheRequest);
public:
    HttpCacheRequest(UTF8CString&& url, ob_httpcache_entry* entry)
        : m_url(WTF::move(url))
        , m_entry(entry)
    {
    }

    ~HttpCacheRequest()
    {
        if (m_writer)
            ob_httpcache_store_abort(s_cache, m_writer);
        if (m_entry)
            ob_httpcache_entry_free(m_entry);
    }

    // The stored body and headers, read once; false if the entry is damaged.
    bool loadBody()
    {
        if (m_body)
            return true;
        if (!m_entry)
            return false;
        Vector<uint8_t> bytes(ob_httpcache_entry_body_size(m_entry));
        if (!bytes.isEmpty() && ob_httpcache_entry_read(m_entry, bytes.mutableSpan().data(), bytes.size()) != static_cast<long>(bytes.size()))
            return false;
        m_body = SharedBuffer::create(WTF::move(bytes));
        for (unsigned i = 0, count = ob_httpcache_entry_header_count(m_entry); i < count; ++i)
            m_headers.append(String::fromUTF8(ob_httpcache_entry_header(m_entry, i)));
        return true;
    }

    void setFresh() { m_fresh = true; }

    bool isFresh() const final { return m_fresh; }

    ResourceResponse cachedResponse() final
    {
        AmigaResponseHead stored;
        stored.url = URL { String::fromUTF8(m_url.legacyCStringPointer()) };
        stored.statusCode = 200;
        stored.expectedContentLength = m_body ? m_body->size() : 0;
        stored.headers.append("HTTP/1.1 200 OK"_s);
        for (auto& header : m_headers)
            stored.headers.append(header);
        ResourceResponse response(stored);
        response.setSource(ResourceResponse::Source::DiskCache);
        return response;
    }

    RefPtr<SharedBuffer> cachedBody() final { return m_body; }

    bool notModified(const ResourceResponse& response) final
    {
        if (!m_entry || !loadBody())
            return false;
        // The 304's headers replace the stored ones of the same name, and
        // the merged set gives the copy's new lifetime, as a 200 would.
        HTTPHeaderMap merged;
        for (auto& line : m_headers) {
            size_t colon = line.find(':');
            if (colon != notFound)
                merged.add(line.left(colon).trim(isASCIIWhitespace<char16_t>), line.substring(colon + 1).trim(isASCIIWhitespace<char16_t>));
        }
        for (auto& header : response.httpHeaderFields()) {
            if (!isStoredHeader(header))
                continue;
            if (header.keyAsHTTPHeaderName)
                merged.set(*header.keyAsHTTPHeaderName, header.value);
            else
                merged.set(header.key, header.value);
        }
        m_headers.clear();
        for (auto& header : merged)
            m_headers.append(makeString(header.key, ": "_s, header.value));

        // The old file is closed first, so it can be replaced or removed.
        ob_httpcache_entry_free(std::exchange(m_entry, nullptr));
        auto now = time(nullptr);
        long lifetime = lifetimeOf(merged, 200);
        if (lifetime < 0) {
            // now no-store: this answer still uses the copy, later ones do not
            ob_httpcache_remove(s_cache, m_url.legacyCStringPointer());
            return true;
        }
        auto* writer = beginStore(merged, lifetime, now);
        if (writer && (!m_body->size() || ob_httpcache_store_write(writer, m_body->span().data(), m_body->size())))
            ob_httpcache_store_commit(s_cache, writer, now);
        else {
            if (writer)
                ob_httpcache_store_abort(s_cache, writer);
            ob_httpcache_remove(s_cache, m_url.legacyCStringPointer());
        }
        return true;
    }

    void willStore(const ResourceResponse& response) final
    {
        if (!s_cache)
            return;
        int status = response.httpStatusCode();
        if (status != 200) {
            // A redirect, a 404 or the like replaces what the address
            // shows, so a stored copy must not answer for it again; a
            // server error leaves it be.
            if (status < 500) {
                if (m_entry)
                    ob_httpcache_entry_free(std::exchange(m_entry, nullptr));
                ob_httpcache_remove(s_cache, m_url.legacyCStringPointer());
            }
            return;
        }
        // A new copy: the old one's file is closed, so it can be replaced
        // (AmigaDOS neither renames onto nor deletes an open file).
        if (m_entry)
            ob_httpcache_entry_free(std::exchange(m_entry, nullptr));
        auto now = time(nullptr);
        long lifetime = lifetimeOf(response.httpHeaderFields(), status);
        if (lifetime < 0) {
            // no-store and the like: an older copy must not answer either
            ob_httpcache_remove(s_cache, m_url.legacyCStringPointer());
            return;
        }
        if (response.httpHeaderFields().contains(HTTPHeaderName::ContentRange))
            return;
        m_writer = beginStore(response.httpHeaderFields(), lifetime, now);
    }

    void storeData(const SharedBuffer& buffer) final
    {
        if (m_writer && !ob_httpcache_store_write(m_writer, buffer.span().data(), buffer.size())) {
            ob_httpcache_store_abort(s_cache, m_writer);
            m_writer = nullptr;
        }
    }

    void storeFinished() final
    {
        if (m_writer)
            ob_httpcache_store_commit(s_cache, std::exchange(m_writer, nullptr), time(nullptr));
    }

private:
    // Headers that must not come back with a stored copy: cookies, and the
    // length and encoding the transport undid.
    static bool isStoredHeader(const HTTPHeaderMap::HTTPHeaderMapConstIterator::KeyValue& header)
    {
        if (auto name = header.keyAsHTTPHeaderName) {
            switch (*name) {
            case HTTPHeaderName::SetCookie:
            case HTTPHeaderName::SetCookie2:
            case HTTPHeaderName::ContentLength:
            case HTTPHeaderName::ContentEncoding:
            case HTTPHeaderName::TransferEncoding:
            case HTTPHeaderName::Connection:
            case HTTPHeaderName::KeepAlive:
                return false;
            default:
                break;
            }
        }
        return true;
    }

    ob_httpcache_writer* beginStore(const HTTPHeaderMap& headers, long lifetime, time_t now)
    {
        Vector<UTF8CString> lines;
        for (auto& header : headers) {
            if (isStoredHeader(header))
                lines.append(makeString(header.key, ": "_s, header.value).utf8());
        }
        Vector<const char*> pointers;
        for (auto& line : lines)
            pointers.append(line.legacyCStringPointer());
        auto etag = headers.get(HTTPHeaderName::ETag).utf8();
        auto lastModified = headers.get(HTTPHeaderName::LastModified).utf8();
        return ob_httpcache_store_begin(s_cache, m_url.legacyCStringPointer(), lifetime, now, etag.legacyCStringPointer(), lastModified.legacyCStringPointer(), pointers.span().data(), pointers.size());
    }

    static long lifetimeOf(const HTTPHeaderMap& headers, int status)
    {
        auto field = [&](HTTPHeaderName name) { return headers.get(name).utf8(); };
        auto cacheControl = field(HTTPHeaderName::CacheControl);
        auto pragma = field(HTTPHeaderName::Pragma);
        auto expires = field(HTTPHeaderName::Expires);
        auto date = field(HTTPHeaderName::Date);
        auto age = field(HTTPHeaderName::Age);
        auto vary = field(HTTPHeaderName::Vary);
        auto etag = field(HTTPHeaderName::ETag);
        auto lastModified = field(HTTPHeaderName::LastModified);
        struct ob_httpcache_response r { };
        r.status = status;
        r.cacheControl = headerOrNull(cacheControl, headers.contains(HTTPHeaderName::CacheControl));
        r.pragma = headerOrNull(pragma, headers.contains(HTTPHeaderName::Pragma));
        r.expires = headerOrNull(expires, headers.contains(HTTPHeaderName::Expires));
        r.date = headerOrNull(date, headers.contains(HTTPHeaderName::Date));
        r.age = headerOrNull(age, headers.contains(HTTPHeaderName::Age));
        r.vary = headerOrNull(vary, headers.contains(HTTPHeaderName::Vary));
        r.etag = headerOrNull(etag, headers.contains(HTTPHeaderName::ETag));
        r.lastModified = headerOrNull(lastModified, headers.contains(HTTPHeaderName::LastModified));
        return ob_httpcache_lifetime(&r, time(nullptr));
    }

    UTF8CString m_url;
    ob_httpcache_entry* m_entry { nullptr };
    ob_httpcache_writer* m_writer { nullptr };
    RefPtr<SharedBuffer> m_body;
    Vector<String> m_headers;
    bool m_fresh { false };
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(HttpCacheRequest);

static std::unique_ptr<AmigaHttpCacheRequest> cacheRequestFor(ResourceRequest& request)
{
    if (!s_cache || !request.url().protocolIsInHTTPFamily())
        return nullptr;
    URL key = request.url();
    key.removeFragmentIdentifier();
    auto url = key.string().utf8();
    auto method = request.httpMethod();
    if (method != "GET"_s) {
        // A POST, PUT or DELETE may change what the address shows.
        if (method != "HEAD"_s && method != "OPTIONS"_s)
            ob_httpcache_remove(s_cache, url.legacyCStringPointer());
        return nullptr;
    }

    auto& headers = request.httpHeaderFields();
    auto cacheControl = headers.get(HTTPHeaderName::CacheControl).utf8();
    auto pragma = headers.get(HTTPHeaderName::Pragma).utf8();
    struct ob_httpcache_request r { };
    r.method = "GET";
    r.cacheControl = headerOrNull(cacheControl, headers.contains(HTTPHeaderName::CacheControl));
    r.pragma = headerOrNull(pragma, headers.contains(HTTPHeaderName::Pragma));
    r.hasAuthorization = headers.contains(HTTPHeaderName::Authorization);
    // WebCore's memory cache asks itself; its 304 must reach it.
    r.hasConditional = headers.contains(HTTPHeaderName::IfNoneMatch) || headers.contains(HTTPHeaderName::IfModifiedSince)
        || headers.contains(HTTPHeaderName::IfRange) || headers.contains(HTTPHeaderName::Range);
    int policy = ob_httpcache_request_policy(&r);
    long maxAge = ob_httpcache_request_max_age(&r);
    if (policy == OB_HTTPCACHE_BYPASS)
        return nullptr;

    // WebCore's cache mode (fetch()'s cache option, reloads, back and forward).
    bool lookUp = true, staleIsFine = false;
    switch (request.cachePolicy()) {
    case ResourceRequestCachePolicy::UseProtocolCachePolicy:
        break;
    case ResourceRequestCachePolicy::ReloadIgnoringCacheData:
        lookUp = false;                     // fetched again, and stored
        break;
    case ResourceRequestCachePolicy::RefreshAnyCacheData:
        policy = OB_HTTPCACHE_REVALIDATE;
        break;
    case ResourceRequestCachePolicy::ReturnCacheDataElseLoad:
    case ResourceRequestCachePolicy::ReturnCacheDataDontLoad:
        staleIsFine = true;
        break;
    case ResourceRequestCachePolicy::DoNotUseAnyCache:
        return nullptr;
    }

    auto now = time(nullptr);
    auto* entry = lookUp ? ob_httpcache_lookup(s_cache, url.legacyCStringPointer(), now) : nullptr;
    auto cacheRequest = makeUnique<HttpCacheRequest>(WTF::move(url), entry);
    if (!entry)
        return cacheRequest;
    bool usable = staleIsFine || (policy == OB_HTTPCACHE_USE && ob_httpcache_entry_fresh(entry, now)
        && (maxAge < 0 || ob_httpcache_entry_age(entry, now) <= maxAge));
    if (usable && cacheRequest->loadBody()) {
        cacheRequest->setFresh();
        return cacheRequest;
    }
    if (const char* etag = ob_httpcache_entry_etag(entry); *etag)
        request.setHTTPHeaderField(HTTPHeaderName::IfNoneMatch, String::fromUTF8(etag));
    if (const char* lastModified = ob_httpcache_entry_last_modified(entry); *lastModified)
        request.setHTTPHeaderField(HTTPHeaderName::IfModifiedSince, String::fromUTF8(lastModified));
    return cacheRequest;
}

bool initializeHttpCache(const char* defaultDir)
{
    s_cache = ob_httpcache_open_from_env(defaultDir);
    if (s_cache)
        setAmigaHttpCache(cacheRequestFor);
    return !!s_cache;
}

void closeHttpCache()
{
    setAmigaHttpCache(nullptr);
    ob_httpcache_close(std::exchange(s_cache, nullptr));
}

} // namespace OpenBrowser
