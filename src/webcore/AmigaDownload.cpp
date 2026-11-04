/*
 * GFOpenBrowser: downloads (see AmigaDownload.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "config.h"
#include "AmigaDownload.h"

#include "ob_webview.h"

#include <WebCore/AuthenticationChallenge.h>
#include <WebCore/AuthenticationClient.h>
#include <WebCore/DataURLDecoder.h>
#include <WebCore/NetworkLoadMetrics.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceHandle.h>
#include <WebCore/ResourceHandleClient.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/SharedBuffer.h>
#include <stdio.h>
#include <string.h>
#include <wtf/HashMap.h>
#include <wtf/MonotonicTime.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RunLoop.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>

namespace GFOpenBrowser {

using namespace WebCore;

static OBDownloadCallback downloadCallback;
static void* downloadContext;
static char downloadFolder[256] = "RAM:";
static int lastDownloadID;

class Download;
static HashMap<int, RefPtr<Download>>& downloads();

// A name AmigaDOS takes: no colon or slash, no control characters, at most
// 100 bytes (FFS takes 107), in Latin-1, as the file system has it.
static void amigaFileName(const String& name, char* out, size_t size)
{
    CString latin1 = name.latin1();
    const char* in = latin1.data();
    size_t n = 0;
    for (size_t i = 0; i < latin1.length() && n < size - 1 && n < 100; ++i) {
        char c = in[i];
        unsigned char u = static_cast<unsigned char>(c);
        out[n++] = u < 0x20 || u == 0x7f || c == ':' || c == '/' || c == '"' || c == '*' || c == '?' ? '_' : c;
    }
    while (n && (out[n - 1] == ' ' || out[n - 1] == '.'))
        --n;
    out[n] = 0;
    if (!n)
        snprintf(out, size, "download");
}

static bool fileExists(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return false;
    fclose(file);
    return true;
}

static String nameFromURL(const URL& url)
{
    auto name = url.lastPathComponent().toString();
    if (name.isEmpty())
        name = url.host().toString();
    return name;
}

class Download final : public ResourceHandleClient, public RefCounted<Download> {
public:
    static Ref<Download> create(int id, const String& suggestedName) { return adoptRef(*new Download(id, suggestedName)); }
    ~Download() { closeFile(); }

    void start(const ResourceRequest& request)
    {
        m_url = request.url().string();
        m_handle = ResourceHandle::create(nullptr, request, this, false, false, ContentEncodingSniffingPolicy::Default, nullptr, false);
        if (!m_handle)
            fail("The download could not start."_s);
    }

    // data: addresses have their bytes in them.
    void startData(const URL& url)
    {
        m_url = url.string();
        auto result = DataURLDecoder::decode(url);
        if (!result) {
            fail("The data: address could not be read."_s);
            return;
        }
        ResourceResponse response(URL { url }, String { result->mimeType }, result->data.size(), String { result->charset });
        if (!openFile(response))
            return;
        if (!result->data.isEmpty() && fwrite(result->data.span().data(), 1, result->data.size(), m_file) != result->data.size()) {
            fail("The file could not be written (is the disk full?)."_s);
            return;
        }
        m_received = result->data.size();
        finish();
    }

    void cancel()
    {
        if (m_ended)
            return;
        releaseHandle(true);
        closeFile();
        if (!m_path.isNull())
            remove(m_path.data());
        end(OB_DOWNLOAD_CANCELLED, nullptr);
    }

private:
    Download(int id, const String& suggestedName)
        : m_id(id)
        , m_suggestedName(suggestedName)
    {
    }

    void willSendRequestAsync(ResourceHandle*, ResourceRequest&& request, ResourceResponse&&, CompletionHandler<void(ResourceRequest&&)>&& completionHandler) final
    {
        completionHandler(WTF::move(request));
    }

    void didReceiveResponseAsync(ResourceHandle*, ResourceResponse&& response, CompletionHandler<void()>&& completionHandler) final
    {
        if (response.httpStatusCode() >= 400) {
            fail(makeString("The server answered "_s, response.httpStatusCode(), ' ', response.httpStatusText(), '.'));
            completionHandler();
            return;
        }
        openFile(response);
        completionHandler();
    }

    // A log-in is asked for the page, not for a download: its 401 fails it.
    void didReceiveAuthenticationChallenge(ResourceHandle*, const AuthenticationChallenge& challenge) final
    {
        if (RefPtr client = challenge.authenticationClient())
            client->receivedRequestToContinueWithoutCredential(challenge);
    }

    void didReceiveData(ResourceHandle*, const SharedBuffer& buffer, int) final
    {
        if (!m_file)
            return;
        if (fwrite(buffer.span().data(), 1, buffer.size(), m_file) != buffer.size()) {
            fail("The file could not be written (is the disk full?)."_s);
            return;
        }
        m_received += buffer.size();
        // Four reports a second at most: each one draws the window's row.
        auto now = MonotonicTime::now();
        if (now - m_lastReport >= 250_ms) {
            m_lastReport = now;
            report(OB_DOWNLOAD_PROGRESS, nullptr);
        }
    }

    void didFinishLoading(ResourceHandle*, const NetworkLoadMetrics&) final
    {
        if (!m_ended)
            finish();
    }

    void didFail(ResourceHandle*, const ResourceError& error) final
    {
        if (error.isCancellation())
            cancel();
        else
            fail(error.localizedDescription().isEmpty() ? String("The download failed."_s) : error.localizedDescription());
    }

    bool openFile(const ResourceResponse& response)
    {
        if (m_file)
            return true;
        String name = response.suggestedFilename();
        if (name.isEmpty())
            name = m_suggestedName;
        if (name.isEmpty())
            name = nameFromURL(response.url());
        char file[104], path[400];
        amigaFileName(name, file, sizeof file);
        size_t folderLength = strlen(downloadFolder);
        const char* separator = folderLength && downloadFolder[folderLength - 1] != ':' && downloadFolder[folderLength - 1] != '/' ? "/" : "";
        snprintf(path, sizeof path, "%s%s%s", downloadFolder, separator, file);
        // Another file there already: "name (2).ext", "name (3).ext"...
        const char* dot = strrchr(file, '.');
        int stem = dot && dot != file ? static_cast<int>(dot - file) : static_cast<int>(strlen(file));
        for (int n = 2; fileExists(path) && n < 100; ++n)
            snprintf(path, sizeof path, "%s%s%.*s (%d)%s", downloadFolder, separator, stem, file, n, file + stem);
        m_file = fopen(path, "wb");
        if (!m_file) {
            fail(makeString("The file "_s, String::fromLatin1(path), " could not be made."_s));
            return false;
        }
        m_path = CString(path);
        m_name = String::fromLatin1(file);
        m_total = response.expectedContentLength() > 0 ? static_cast<unsigned long>(response.expectedContentLength()) : 0;
        m_lastReport = MonotonicTime::now();
        report(OB_DOWNLOAD_STARTED, nullptr);
        return true;
    }

    // The handle may be calling this download now: it goes from the run loop.
    void releaseHandle(bool cancel)
    {
        RefPtr handle = std::exchange(m_handle, nullptr);
        if (!handle)
            return;
        handle->clearClient();
        if (cancel)
            handle->cancel();
        RunLoop::mainSingleton().dispatch([handle = WTF::move(handle)] { });
    }

    void closeFile()
    {
        if (m_file)
            fclose(m_file);
        m_file = nullptr;
    }

    void finish()
    {
        closeFile();
        releaseHandle(false);
        end(OB_DOWNLOAD_DONE, nullptr);
    }

    void fail(const String& message)
    {
        if (m_ended)
            return;
        releaseHandle(true);
        bool hadFile = m_file;
        closeFile();
        if (hadFile && !m_path.isNull())
            remove(m_path.data());
        end(OB_DOWNLOAD_FAILED, message.utf8().data());
    }

    void end(int state, const char* error)
    {
        m_ended = true;
        report(state, error);
        // Forgotten from the run loop: this may be inside one of its own calls.
        RunLoop::mainSingleton().dispatch([id = m_id] {
            downloads().remove(id);
        });
    }

    void report(int state, const char* error)
    {
        if (!downloadCallback)
            return;
        CString name = m_name.isNull() ? nameFromURL(URL { m_url }).utf8() : m_name.utf8();
        downloadCallback(downloadContext, m_id, state, name.data(), m_path.isNull() ? "" : m_path.data(), m_received, m_total, error);
    }

    int m_id;
    String m_suggestedName;
    String m_url;
    String m_name;
    CString m_path;
    RefPtr<ResourceHandle> m_handle;
    FILE* m_file { nullptr };
    unsigned long m_received { 0 };
    unsigned long m_total { 0 };
    MonotonicTime m_lastReport;
    bool m_ended { false };
};

static HashMap<int, RefPtr<Download>>& downloads()
{
    static NeverDestroyed<HashMap<int, RefPtr<Download>>> map;
    return map;
}

int startDownload(const ResourceRequest& request, const String& suggestedName)
{
    int id = ++lastDownloadID;
    Ref download = Download::create(id, suggestedName);
    downloads().set(id, download.ptr());
    if (request.url().protocolIsData())
        download->startData(request.url());
    else
        download->start(request);
    return id;
}

} // namespace GFOpenBrowser

using namespace GFOpenBrowser;

void ob_download_set_callback(OBDownloadCallback callback, void* context)
{
    downloadCallback = callback;
    downloadContext = context;
}

void ob_download_set_folder(const char* folder)
{
    if (!folder || !*folder)
        folder = "RAM:";
    strncpy(downloadFolder, folder, sizeof downloadFolder - 1);
    downloadFolder[sizeof downloadFolder - 1] = 0;
}

int ob_download_url(const char* url)
{
    if (!url || !*url)
        return 0;
    WTF::URL address { String::fromUTF8(url) };
    if (!address.isValid())
        return 0;
    WebCore::ResourceRequest request(address);
    request.setFirstPartyForCookies(request.url());
    return startDownload(request, { });
}

void ob_download_cancel(int id)
{
    auto it = downloads().find(id);
    if (it != downloads().end())
        RefPtr { it->value }->cancel();
}
