/*
 * GFOpenBrowser: the cookie jar. Cookies live in one SQLite database
 * (WebCore's CookieJarDB), shared by document.cookie and the network loader.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#pragma once

#include <WebCore/CookieJar.h>
#include <WebCore/CookieJarDB.h>
#include <wtf/UniqueRef.h>

namespace GFOpenBrowser {

class AmigaCookieJar final : public WebCore::CookieJar {
public:
    // databasePath is an AmigaDOS name, such as "PROGDIR:Cookies.db", or
    // ":memory:" for cookies that last until the browser quits.
    static Ref<AmigaCookieJar> create(const String& databasePath);

    static AmigaCookieJar* shared();

    WebCore::CookieJarDB& database() const;

    // Cookies for the network: the Cookie header for a request, and the
    // Set-Cookie headers of a response.
    String cookieHeaderForRequest(const URL& firstParty, const URL&) const;
    void storeFromResponse(const URL& firstParty, const URL&, const String& setCookieHeader) const;

    // WebCore::CookieJar
    String cookies(WebCore::Document&, const URL&) const final;
    void setCookies(WebCore::Document&, const URL&, const String& cookieString) final;
    bool cookiesEnabled(WebCore::Document&) final { return true; }
    std::pair<String, WebCore::SecureCookiesAccessed> cookieRequestHeaderFieldValue(const URL& firstParty, const WebCore::SameSiteInfo&, const URL&, WebCore::IncludeSecureCookies) const final;
    bool getRawCookies(WebCore::Document&, const URL&, Vector<WebCore::Cookie>&) const final;
    void setRawCookie(const WebCore::Document&, const WebCore::Cookie&, WebCore::ShouldPartitionCookie) final;
    void deleteCookie(const WebCore::Document&, const URL&, const String& cookieName, CompletionHandler<void()>&&) final;

private:
    explicit AmigaCookieJar(const String& databasePath);

    mutable UniqueRef<WebCore::CookieJarDB> m_database;
};

} // namespace GFOpenBrowser
