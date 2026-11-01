/*
 * javascript.library engine: translate once (jsl_cache.h).
 *
 * JavaScriptCore asks a script's SourceProvider for translations it saved
 * before (cachedBytecode), gives it the script's when it makes them
 * (cacheBytecode), and each function's when that function is first called
 * (updateCache). This provider keeps them in one file per script text in
 * the task's cache folder, named by a hash and the length of the text, and
 * writes what is new when told to (jsl_cache_commit) or when it goes. It is
 * what jsc's --diskCachePath does, with plain C files so the Amiga needs no
 * file locks or mapping.
 *
 * Only the engine's private interface is used here, so this file is built
 * with JavaScriptCore's own flags (config.h); jsl_core.cpp stays on the C API.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "config.h"
#include "jsl_cache.h"

#include "APICast.h"
#include "BytecodeCacheError.h"
#include "CachedBytecode.h"
#include "CachedTypes.h"
#include "CodeCache.h"
#include "Completion.h"
#include "Exception.h"
#include "JSCInlines.h"
#include "JSLock.h"
#include "OpaqueJSString.h"
#include "SourceCode.h"
#include "SourceProvider.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <wtf/MallocSpan.h>
#include <wtf/Vector.h>
#include <wtf/text/StringView.h>

using namespace JSC;

namespace {

constexpr size_t pathSize = 300;

class CacheProvider final : public StringSourceProvider {
public:
    static Ref<CacheProvider> create(const String& source, const SourceOrigin& origin, String&& url, const char* path)
    {
        return adoptRef(*new CacheProvider(source, origin, WTF::move(url), path));
    }

    ~CacheProvider() final
    {
        commitCachedBytecode();
    }

    RefPtr<CachedBytecode> cachedBytecode() const final
    {
        if (!m_cachedBytecode && !m_loaded) {
            m_loaded = true;
            load();
        }
        return m_cachedBytecode.copyRef();
    }

    // Only called when the script was made afresh: no file, or one from
    // another engine version. Either way the file is written anew.
    void cacheBytecode(const BytecodeCacheGenerator& generator) const final
    {
        m_cachedBytecode = CachedBytecode::create();
        if (auto update = generator())
            m_cachedBytecode->addGlobalUpdate(*update);
    }

    void updateCache(const UnlinkedFunctionExecutable* executable, const SourceCode&, CodeSpecializationKind kind, const UnlinkedFunctionCodeBlock* codeBlock) const final
    {
        if (!m_cachedBytecode)
            return;
        BytecodeCacheError error;
        RefPtr<CachedBytecode> function = encodeFunctionCodeBlock(executable->vm(), codeBlock, error);
        if (function && !error.isValid())
            m_cachedBytecode->addFunctionUpdate(executable, kind, *function);
    }

    void commitCachedBytecode() const final
    {
        if (!m_cachedBytecode || !m_cachedBytecode->hasUpdates())
            return;
        Ref<CachedBytecode> bytecode = *m_cachedBytecode;
        m_cachedBytecode = nullptr;     // what is written is not added to again
        size_t had = bytecode->size();
        FILE* file = fopen(m_path, had ? "r+b" : "wb");
        if (!file)
            return;
        // The file must still be the one read: another task may have written it
        if (had && (fseek(file, 0, SEEK_END) || static_cast<size_t>(ftell(file)) != had)) {
            fclose(file);
            return;
        }
        bool ok = true;
        bytecode->commitUpdates([&] (off_t offset, std::span<const uint8_t> data) {
            if (ok && (fseek(file, static_cast<long>(offset), SEEK_SET) || fwrite(data.data(), 1, data.size(), file) != data.size()))
                ok = false;
        });
        if (fclose(file) || !ok)
            remove(m_path);      // half a file is no file
    }

private:
    CacheProvider(const String& source, const SourceOrigin& origin, String&& url, const char* path)
        : StringSourceProvider(source, origin, SourceTaintedOrigin::Untainted, WTF::move(url), TextPosition(), SourceProviderSourceType::Program)
    {
        snprintf(m_path, sizeof m_path, "%s", path);
    }

    void load() const
    {
        FILE* file = fopen(m_path, "rb");
        if (!file)
            return;
        long size = -1;
        if (!fseek(file, 0, SEEK_END))
            size = ftell(file);
        if (size > 0 && !fseek(file, 0, SEEK_SET)) {
            auto data = MallocSpan<uint8_t, VMMalloc>::tryMalloc(size);
            if (data && fread(data.mutableSpan().data(), 1, size, file) == static_cast<size_t>(size))
                m_cachedBytecode = CachedBytecode::create(WTF::move(data), { });
        }
        fclose(file);
    }

    char m_path[pathSize];
    mutable RefPtr<CachedBytecode> m_cachedBytecode;
    mutable bool m_loaded { false };
};

// FNV-1a over the text's UTF-16 or Latin-1 units, with its length beside it
// in the name: two texts share a file only if both match.
uint32_t textHash(StringView text)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < text.length(); i++) {
        UChar c = text[i];
        h = (h ^ (c & 0xff)) * 16777619u;
        h = (h ^ (c >> 8)) * 16777619u;
    }
    return h;
}

void cachePath(char (&path)[pathSize], const char* dir, StringView text)
{
    size_t n = strlen(dir);
    const char* slash = n && dir[n - 1] != '/' && dir[n - 1] != ':' ? "/" : "";
    snprintf(path, pathSize, "%s%s%08lx-%08lx.jsc", dir, slash,
        static_cast<unsigned long>(textHash(text)), static_cast<unsigned long>(text.length()));
}

// The folder, made if it is not there (its parent must be)
void makeFolder(const char* dir)
{
    char path[pathSize];
    size_t n = strlen(dir);
    if (!n || n >= pathSize || dir[n - 1] == ':')
        return;
    memcpy(path, dir, n + 1);
    if (path[n - 1] == '/')
        path[n - 1] = 0;
    mkdir(path, 0777);
}

} // namespace

struct JSLCacheList {
    Vector<Ref<CacheProvider>> providers;
};

JSValueRef jsl_cache_evaluate(JSContextRef ctx, JSStringRef script, JSStringRef urlString, const char* cacheDir, JSLCacheList** list, JSValueRef* exception)
{
    if (!cacheDir || !*cacheDir || !Options::useCodeCache())
        return JSEvaluateScript(ctx, script, nullptr, urlString, 1, exception);

    JSGlobalObject* globalObject = toJS(ctx);
    VM& vm = globalObject->vm();
    JSLockHolder locker(vm);

    String text = script->string();
    auto url = urlString ? URL({ }, urlString->string()) : URL();
    if (!*list) {
        *list = new JSLCacheList;
        makeFolder(cacheDir);
    }
    char path[pathSize];
    cachePath(path, cacheDir, text);
    Ref<CacheProvider> provider = CacheProvider::create(text, SourceOrigin { url }, String(url.string()), path);
    (*list)->providers.append(provider);
    SourceCode source(WTF::move(provider));

    NakedPtr<Exception> evaluationException;
    JSValue value = profiledEvaluate(globalObject, ProfilingReason::API, source, JSValue(), evaluationException);
    if (evaluationException) {
        if (exception)
            *exception = toRef(globalObject, evaluationException->value());
        return nullptr;
    }
    return toRef(globalObject, value ? value : jsUndefined());
}

void jsl_cache_commit(JSLCacheList** list)
{
    if (!*list)
        return;
    for (auto& provider : (*list)->providers)
        provider->commitCachedBytecode();
    delete *list;
    *list = nullptr;
}
