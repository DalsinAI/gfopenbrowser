/*
 * GFOpenBrowser: sessionStorage in memory (see AmigaSessionStorage.h).
 * Each view has one page and one provider; the provider keeps one session
 * namespace, and that keeps one area per origin. Nothing is written to disk.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "config.h"
#include "AmigaSessionStorage.h"

#include <WebCore/LocalFrame.h>
#include <WebCore/Page.h>
#include <WebCore/SecurityOrigin.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/StorageArea.h>
#include <WebCore/StorageMap.h>
#include <WebCore/StorageNamespace.h>
#include <WebCore/StorageType.h>
#include <pal/SessionID.h>
#include <wtf/HashMap.h>

namespace GFOpenBrowser {

using namespace WebCore;

namespace {

// 5 MB per origin, as other browsers allow.
constexpr unsigned sessionStorageQuota = 5 * 1024 * 1024;

class MemoryStorageArea final : public StorageArea {
public:
    static Ref<MemoryStorageArea> create(StorageType type) { return adoptRef(*new MemoryStorageArea(type)); }

private:
    explicit MemoryStorageArea(StorageType type)
        : m_map(sessionStorageQuota)
        , m_type(type)
    {
    }

    unsigned length() final { return m_map.length(); }
    String key(unsigned index) final { return m_map.key(index); }
    String item(const String& key) final { return m_map.getItem(key); }
    void setItem(LocalFrame&, const String& key, const String& value, bool& quotaException) final
    {
        String oldValue;
        m_map.setItem(key, value, oldValue, quotaException);
    }
    void removeItem(LocalFrame&, const String& key) final
    {
        String oldValue;
        m_map.removeItem(key, oldValue);
    }
    void clear(LocalFrame&) final { m_map.clear(); }
    bool contains(const String& key) final { return m_map.contains(key); }
    StorageType storageType() const final { return m_type; }
    size_t memoryBytesUsedByCache() final { return 0; }

    StorageMap m_map;
    StorageType m_type;
};

class MemoryStorageNamespace final : public StorageNamespace {
public:
    static Ref<MemoryStorageNamespace> create(StorageType type, PAL::SessionID sessionID) { return adoptRef(*new MemoryStorageNamespace(type, sessionID)); }

private:
    MemoryStorageNamespace(StorageType type, PAL::SessionID sessionID)
        : m_type(type)
        , m_sessionID(sessionID)
    {
    }

    Ref<StorageArea> storageArea(const SecurityOrigin& origin) final
    {
        return m_areas.ensure(origin.data(), [&] {
            return MemoryStorageArea::create(m_type);
        }).iterator->value;
    }
    const SecurityOrigin* topLevelOrigin() const final { return nullptr; }
    // A new window starts with an empty session (no window.open copies yet).
    Ref<StorageNamespace> copy(Page&) final { return create(m_type, m_sessionID); }
    PAL::SessionID sessionID() const final { return m_sessionID; }
    void setSessionIDForTesting(PAL::SessionID sessionID) final { m_sessionID = sessionID; }

    StorageType m_type;
    PAL::SessionID m_sessionID;
    HashMap<SecurityOriginData, Ref<MemoryStorageArea>> m_areas;
};

class MemoryStorageNamespaceProvider final : public StorageNamespaceProvider {
private:
    RefPtr<StorageNamespace> sessionStorageNamespace(const SecurityOrigin&, Page& page, ShouldCreateNamespace shouldCreate) final
    {
        if (!m_session && shouldCreate == ShouldCreateNamespace::Yes)
            m_session = MemoryStorageNamespace::create(StorageType::Session, page.sessionID());
        return m_session;
    }
    // localStorage stays off (Settings::localStorageEnabled() is false), so
    // these are only made if a page ever reaches them: in memory as well.
    Ref<StorageNamespace> createLocalStorageNamespace(unsigned, PAL::SessionID sessionID) final { return MemoryStorageNamespace::create(StorageType::Local, sessionID); }
    Ref<StorageNamespace> createTransientLocalStorageNamespace(SecurityOrigin&, unsigned, PAL::SessionID sessionID) final { return MemoryStorageNamespace::create(StorageType::TransientLocal, sessionID); }

    RefPtr<StorageNamespace> m_session;
};

} // namespace

Ref<StorageNamespaceProvider> createSessionStorageProvider()
{
    return adoptRef(*new MemoryStorageNamespaceProvider);
}

} // namespace GFOpenBrowser
