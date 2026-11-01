/*
 * GFOpenBrowser: the cookie jar (see AmigaCookieJar.h). The cookie string
 * logic follows WebKit's curl network session (NetworkStorageSessionCurl).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "config.h"
#include "AmigaCookieJar.h"

#include <WebCore/Cookie.h>
#include <WebCore/Document.h>
#include <WebCore/DocumentInlines.h>
#include <WebCore/SameSiteInfo.h>
#include <WebCore/StorageSessionProvider.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>

namespace GFOpenBrowser {

using namespace WebCore;

namespace {
// Cookies do not come from a CookieStorageSession on this port.
class NoStorageSessionProvider final : public StorageSessionProvider {
public:
    CookieStorageSession* storageSession() const final { return nullptr; }
};
}

static AmigaCookieJar* s_shared;

Ref<AmigaCookieJar> AmigaCookieJar::create(const String& databasePath)
{
    Ref jar = adoptRef(*new AmigaCookieJar(databasePath));
    s_shared = jar.ptr();
    return jar;
}

AmigaCookieJar* AmigaCookieJar::shared()
{
    return s_shared;
}

AmigaCookieJar::AmigaCookieJar(const String& databasePath)
    : CookieJar(adoptRef(*new NoStorageSessionProvider))
    , m_database(makeUniqueRef<CookieJarDB>(databasePath))
{
}

CookieJarDB& AmigaCookieJar::database() const
{
    m_database->open();
    return m_database;
}

static std::pair<String, bool> cookieString(CookieJarDB& database, const URL& firstParty, const URL& url, bool forHTTPHeader, IncludeSecureCookies includeSecureCookies)
{
    StringBuilder cookies;
    bool didAccessSecureCookies = false;
    auto httpOnly = forHTTPHeader ? std::nullopt : std::optional<bool> { false };
    auto secure = includeSecureCookies == IncludeSecureCookies::Yes ? std::nullopt : std::optional<bool> { false };
    if (auto result = database.searchCookies(firstParty, url, httpOnly, secure, std::nullopt)) {
        for (const auto& cookie : *result) {
            if (!cookies.isEmpty())
                cookies.append("; "_s);
            if (!cookie.name.isEmpty())
                cookies.append(cookie.name, '=');
            if (cookie.secure)
                didAccessSecureCookies = true;
            cookies.append(cookie.value);
        }
    }
    return { cookies.toString(), didAccessSecureCookies };
}

String AmigaCookieJar::cookieHeaderForRequest(const URL& firstParty, const URL& url) const
{
    return cookieString(database(), firstParty, url, true, shouldIncludeSecureCookies(url)).first;
}

void AmigaCookieJar::storeFromResponse(const URL& firstParty, const URL& url, const String& setCookieHeader) const
{
    database().setCookie(firstParty, url, setCookieHeader, CookieJarDB::Source::Network);
}

String AmigaCookieJar::cookies(Document& document, const URL& url) const
{
    auto result = cookieString(database(), document.firstPartyForCookies(), url, false, shouldIncludeSecureCookies(url));
    if (result.second)
        document.setSecureCookiesAccessed();
    return result.first;
}

void AmigaCookieJar::setCookies(Document& document, const URL& url, const String& cookieString)
{
    database().setCookie(document.firstPartyForCookies(), url, cookieString, CookieJarDB::Source::Script);
}

std::pair<String, SecureCookiesAccessed> AmigaCookieJar::cookieRequestHeaderFieldValue(const URL& firstParty, const SameSiteInfo&, const URL& url, IncludeSecureCookies includeSecureCookies) const
{
    auto result = cookieString(database(), firstParty, url, true, includeSecureCookies);
    return { result.first, result.second ? SecureCookiesAccessed::Yes : SecureCookiesAccessed::No };
}

bool AmigaCookieJar::getRawCookies(Document& document, const URL& url, Vector<Cookie>& rawCookies) const
{
    auto result = database().searchCookies(document.firstPartyForCookies(), url, std::nullopt, std::nullopt, std::nullopt);
    if (!result)
        return false;
    rawCookies = WTF::move(*result);
    return true;
}

void AmigaCookieJar::setRawCookie(const Document&, const Cookie& cookie, ShouldPartitionCookie)
{
    database().setCookie(cookie);
}

void AmigaCookieJar::deleteCookie(const Document&, const URL& url, const String& cookieName, CompletionHandler<void()>&& completionHandler)
{
    database().deleteCookie(url.string(), cookieName);
    completionHandler();
}

} // namespace GFOpenBrowser
