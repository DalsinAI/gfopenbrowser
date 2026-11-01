/*
 * obcore-dump: load a local HTML file into WebCore, print its render tree
 * (milestone 2a) and, given a file name, paint the page into an offscreen
 * bitmap saved as PNG (milestone 2b) through a PNG datatype. The page has
 * no network yet.
 *
 *   obcore-dump <file.html> [width] [page.png]
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "config.h"

#include "AmigaPlatformStrategies.h"

#include <JavaScriptCore/InitializeThreading.h>
#include <WebCore/CommonAtomStrings.h>
#include <WebCore/Document.h>
#include <WebCore/DocumentView.h>
#include <WebCore/DocumentLoader.h>
#include <WebCore/DocumentWriter.h>
#include <WebCore/EmptyClients.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/GraphicsContextCairo.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h>
#include <WebCore/LocalFrameView.h>
#include <WebCore/Page.h>
#include <WebCore/PageConfiguration.h>
#include <WebCore/RenderTreeAsText.h>
#include <WebCore/Settings.h>
#include <WebCore/SharedBuffer.h>
#include <WebCore/WebCoreJITOperations.h>
#include <cairo.h>
#include <cstdio>
#include <cstdlib>
#include <wtf/FileSystem.h>
#include <wtf/MainThread.h>
#include <wtf/URL.h>
#include <wtf/text/CString.h>

extern "C" {
#include "oam_stack.h"
void ob_quiet_requesters(void);
// WebCore's datatypes glue (platform/image-decoders/amiga/DatatypesDecode.h)
int obdt_encode_png(const unsigned char* pixels, unsigned width, unsigned height, unsigned stride,
    unsigned flags, unsigned char** data, unsigned long* size);
void obdt_free_encoded(unsigned char* data);
}
#if OB_ICU_DATA_FILE
#include "ob_icudata.h"
#endif

using namespace WebCore;

static const char version[] __attribute__((used)) = "$VER: obcore-dump 0.1 (4.10.2026)";

static int dumpMain(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: obcore-dump <file.html> [width]\n");
        return 10;
    }
    int width = argc > 2 ? std::atoi(argv[2]) : 800;
    if (width < 100)
        width = 800;

    ob_quiet_requesters();
#if OB_ICU_DATA_FILE
    if (!ob_icu_data_load())
        return 20;
#endif
    JSC::initialize();
    WTF::initializeMainThread();
    initializeCommonAtomStrings();
    populateJITOperations();
    GFOpenBrowser::initializePlatformStrategies();
    std::printf("OBCORE_INIT\n");
    std::fflush(stdout);

    auto contents = FileSystem::readEntireFile(String::fromUTF8(argv[1]));
    if (!contents) {
        std::printf("OBCORE_FAIL cannot read %s\n", argv[1]);
        return 20;
    }

    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, PAL::SessionID::defaultSessionID());
    Ref page = Page::create(WTF::move(configuration));
    page->settings().setScriptEnabled(false);
    page->settings().setAcceleratedCompositingEnabled(false);
    page->settings().setStandardFontFamily("Liberation Sans"_s);
    page->settings().setSansSerifFontFamily("Liberation Sans"_s);
    page->settings().setSerifFontFamily("Liberation Serif"_s);
    page->settings().setFixedFontFamily("Liberation Mono"_s);

    RefPtr frame = page->localMainFrame();
    if (!frame) {
        std::printf("OBCORE_FAIL no main frame\n");
        return 20;
    }
    frame->setView(LocalFrameView::create(*frame, IntSize(width, 600)));
    frame->init();
    std::printf("OBCORE_PAGE width=%d bytes=%u\n", width, static_cast<unsigned>(contents->size()));
    std::fflush(stdout);

    RefPtr loader = frame->loader().activeDocumentLoader();
    if (!loader) {
        std::printf("OBCORE_FAIL no document loader\n");
        return 20;
    }
    loader->writer().setMIMEType("text/html"_s);
    loader->writer().begin(URL { "file:///obcore-dump.html"_s });
    loader->writer().addData(SharedBuffer::create(WTF::move(*contents)));
    loader->writer().end();

    // ROM math libraries opened along the way can leave the FPU in single
    // precision; layout and painting need doubles.
    __asm__ volatile ("fmove.l %0,%%fpcr" : : "d" (0));

    RefPtr document = frame->document();
    document->updateLayoutIgnorePendingStylesheets();
    std::printf("OBCORE_LAYOUT_DONE\n");

    String tree = externalRepresentation(frame.get());
    CString utf8 = tree.utf8();
    std::fwrite(utf8.data(), 1, utf8.length(), stdout);
    std::fflush(stdout);

    if (argc > 3) {
        // Milestone 2b: paint the laid-out page into a cairo image surface.
        RefPtr view = frame->view();
        IntSize contentsSize = view->contentsSize();
        int height = std::max(1, std::min(contentsSize.height(), 4000));
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        {
            GraphicsContextCairo context(surface);
            context.fillRect(FloatRect(0, 0, width, height), Color::white);
            view->paintContents(context, IntRect(0, 0, width, height));
        }
        cairo_surface_flush(surface);
        unsigned char* png = nullptr;
        unsigned long pngSize = 0;
        const char* status = "no PNG datatype that writes";
        if (obdt_encode_png(cairo_image_surface_get_data(surface), width, height, cairo_image_surface_get_stride(surface),
            1 /* premultiplied */, &png, &pngSize)) {
            FILE* file = std::fopen(argv[3], "wb");
            status = file && std::fwrite(png, 1, pngSize, file) == pngSize ? "success" : "write error";
            if (file)
                std::fclose(file);
            obdt_free_encoded(png);
        }
        std::printf("OBCORE_PAINTED %dx%d (page %dx%d) png=%s\n", width, height,
            contentsSize.width(), contentsSize.height(), status);
        cairo_surface_destroy(surface);
    }

    std::printf("OBCORE_DONE\n");
    std::fflush(stdout);
    return 0;
}

int main(int argc, char** argv)
{
    // WebCore and JavaScriptCore recurse deeply; give them 2 MB of stack.
    return oam_run_with_stack(2 * 1024 * 1024, dumpMain, argc, argv);
}
