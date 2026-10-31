/*
 * OpenBrowser: the C interface to WebCore pages (see ob_webview.h), and the
 * translation of Amiga input into WebCore's events.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "config.h"
#include "ob_webview.h"

#include "AmigaCookieJar.h"
#include "AmigaHttpCache.h"
#include "AmigaLoaderStrategy.h"
#include "AmigaNetwork.h"
#include "AmigaPlatformStrategies.h"
#include "AmigaWebView.h"
#include "ob_network.h"
#if OB_ICU_DATA_FILE
#include "ob_icudata.h"
#endif

#include <JavaScriptCore/InitializeThreading.h>
#include <JavaScriptCore/Options.h>
#include <WebCore/DocumentPage.h>
#include <WebCore/DocumentView.h>
#include <WebCore/FrameDestructionObserverInlines.h>
#include <WebCore/NodeDocument.h>
#include <WebCore/CommonAtomStrings.h>
#include <WebCore/EventHandler.h>
#include <WebCore/HTMLElement.h>
#include <WebCore/Document.h>
#include <WebCore/FocusController.h>
#include <WebCore/HandleUserInputEventResult.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h>
#include <WebCore/MemoryCache.h>
#include <WebCore/Page.h>
#include <WebCore/PlatformKeyboardEvent.h>
#include <WebCore/PlatformMouseEvent.h>
#include <WebCore/PlatformWheelEvent.h>
#include <WebCore/ScrollingCoordinatorTypes.h>
#include <WebCore/WebCoreJITOperations.h>
#include <WebCore/WindowsKeyboardCodes.h>
#include <limits>
#include <stdio.h>
#include <time.h>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/MonotonicTime.h>
#include <wtf/RunLoop.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>

namespace WTF {
// RunLoopGeneric.cpp (OS(AMIGA)): the next timer on the clock below, -1 for
// none, 0 for at once.
int64_t amigaRunLoopNextFireMicroseconds();
// CurrentTime.cpp (OS(AMIGA)): MonotonicTime's clock in whole microseconds.
uint64_t amigaMonotonicMicroseconds();
}

using namespace WebCore;

struct OBWebView {
    WTF_MAKE_TZONE_ALLOCATED(OBWebView);
public:
    std::unique_ptr<OpenBrowser::WebView> view;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(OBWebView);

static void resetFPCR()
{
    __asm__ volatile ("fmove.l %0,%%fpcr" : : "d" (0));
}

extern "C" void ob_quiet_requesters(void);

static void initializeEngine(WebCore::LoaderStrategy* loader)
{
    // One CPU: no garbage collection helper threads, which on a 68k only add
    // task switches.
    JSC::initialize([] {
        JSC::Options::setOptions("numberOfGCMarkers=1 useConcurrentGC=false useParallelMarkingConstraintSolver=false");
    });
    WTF::initializeMainThread();
    initializeCommonAtomStrings();
    populateJITOperations();
    OpenBrowser::initializePlatformStrategies(loader);
    // Decoded pictures, style sheets and scripts kept for reuse: 16 MB.
    MemoryCache::singleton().setCapacities(1024 * 1024, 8 * 1024 * 1024, 16 * 1024 * 1024);
    resetFPCR();
}

int ob_webcore_init(void)
{
    ob_quiet_requesters();
#if OB_ICU_DATA_FILE
    if (!ob_icu_data_load())
        return 0;
#endif
    initializeEngine(nullptr);
    return 1;
}

static RefPtr<OpenBrowser::AmigaCookieJar>& cookieJar()
{
    static NeverDestroyed<RefPtr<OpenBrowser::AmigaCookieJar>> jar;
    return jar.get();
}

int ob_webcore_init_with_network(const char* cookieDatabase)
{
    ob_quiet_requesters();
#if OB_ICU_DATA_FILE
    if (!ob_icu_data_load())
        return 0;
#endif
    if (!ob_network_open())
        return 0;
    initializeEngine(OpenBrowser::networkLoaderStrategy());

    // The network thread runs a step above the browser (AmigaNetworkSetup.c).
    AmigaNetworkThreadHooks threadHooks;
    threadHooks.started = ob_network_thread_started;
    setAmigaNetworkThreadHooks(threadHooks);

    // One cookie jar for the network and for document.cookie.
    cookieJar() = OpenBrowser::AmigaCookieJar::create(String::fromUTF8(cookieDatabase ? cookieDatabase : ":memory:"));
    AmigaCookieHooks cookieHooks;
    cookieHooks.cookieHeader = [](const URL& firstParty, const URL& url) {
        auto* jar = OpenBrowser::AmigaCookieJar::shared();
        return jar ? jar->cookieHeaderForRequest(firstParty, url) : String();
    };
    cookieHooks.storeFromResponse = [](const URL& firstParty, const URL& url, const String& setCookie) {
        if (auto* jar = OpenBrowser::AmigaCookieJar::shared())
            jar->storeFromResponse(firstParty, url, setCookie);
    };
    setAmigaCookieHooks(WTF::move(cookieHooks));

    // Pages seen before load their files from the disk (ob_httpcache.h:
    // ENV:OB_HTTP_CACHE=0 turns this off).
    OpenBrowser::initializeHttpCache("PROGDIR:Cache");
    return 1;
}

/* TLS sessions across runs were curl's session cache, saved at quit and
 * read back at start. openweb.fetch keeps no TLS sessions yet (resumption
 * is on its list for later), so since WebKit patch 0075 nothing is read or
 * written: each site does a whole handshake once per run. */
int ob_webview_load_tls_sessions(const char*)
{
    return 0;
}

void ob_webview_save_tls_sessions(const char*)
{
}

namespace WTF {
void amigaStopAllRunLoops();
}

void ob_webcore_stop_threads(void)
{
    // libpthread waits for every thread when the program exits, and WebKit's
    // work-queue threads wait for work for ever: stop their run loops.
    WTF::amigaStopAllRunLoops();
}

void ob_webcore_shutdown(void)
{
    // The network thread ends first: the program cannot exit while it runs.
    stopAmigaNetwork();
    ob_webcore_stop_threads();
    OpenBrowser::closeHttpCache();
    cookieJar() = nullptr;
    ob_network_close();
}

void ob_webcore_cycle(void)
{
    resetFPCR();
    RunLoop::cycle();
}

long ob_webcore_next_timer_micros(void)
{
    // Integers only: one clock reading, no floating point.
    int64_t next = WTF::amigaRunLoopNextFireMicroseconds();
    if (next <= 0)
        return next < 0 ? -1 : 0;
    int64_t wait = next - static_cast<int64_t>(WTF::amigaMonotonicMicroseconds());
    if (wait <= 0)
        return 0;
    return wait < std::numeric_limits<long>::max() ? static_cast<long>(wait) : std::numeric_limits<long>::max();
}

double ob_webcore_next_timer(void)
{
    long wait = ob_webcore_next_timer_micros();
    return wait < 0 ? -1 : wait / 1000000.0;
}

void ob_webcore_set_wakeup(void (*wakeup)(void*), void* context)
{
    RunLoop::setWakeUpCallback([wakeup, context] {
        if (wakeup)
            wakeup(context);
    });
}

OBWebView* ob_webview_create(int width, int height, const OBWebViewCallbacks* callbacks)
{
    OBWebViewCallbacks none { };
    auto* handle = new OBWebView;
    handle->view = makeUnique<OpenBrowser::WebView>(callbacks ? *callbacks : none, IntSize(width, height));
    return handle;
}

void ob_webview_destroy(OBWebView* handle)
{
    delete handle;
}

void ob_webview_load(OBWebView* handle, const char* url)
{
    handle->view->load(String::fromUTF8(url));
}

void ob_webview_load_html(OBWebView* handle, const char* html, const char* baseURL)
{
    handle->view->loadHTML(String::fromUTF8(html), String::fromUTF8(baseURL ? baseURL : ""));
}

void ob_webview_load_data(OBWebView* handle, const char* text, const char* mimeType, const char* baseURL)
{
    handle->view->loadData(String::fromUTF8(text), String::fromUTF8(mimeType ? mimeType : ""), String::fromUTF8(baseURL ? baseURL : ""));
}

void ob_webview_back(OBWebView* handle) { handle->view->goBack(); }
void ob_webview_forward(OBWebView* handle) { handle->view->goForward(); }
void ob_webview_reload(OBWebView* handle) { handle->view->reload(); }
void ob_webview_stop(OBWebView* handle) { handle->view->stop(); }
int ob_webview_can_go_back(OBWebView* handle) { return handle->view->canGoBack(); }
int ob_webview_can_go_forward(OBWebView* handle) { return handle->view->canGoForward(); }

void ob_webview_resize(OBWebView* handle, int width, int height)
{
    handle->view->resize(IntSize(width, height));
}

void ob_webview_paint(OBWebView* handle, unsigned char* argb, int stride, int x, int y, int width, int height)
{
    handle->view->paint(argb, stride, IntRect(x, y, width, height));
}

unsigned long ob_webview_bytes_received(OBWebView* handle)
{
    return static_cast<unsigned long>(handle->view->bytesReceived());
}

int ob_webview_text(OBWebView* handle, char* buffer, int size)
{
    auto* frame = handle->view->mainFrame();
    RefPtr document = frame ? frame->document() : nullptr;
    RefPtr root = document ? document->documentElement() : nullptr;
    if (buffer && size > 0)
        buffer[0] = 0;
    if (!root)
        return 0;
    CString text = protect(*root)->innerText().utf8();
    if (buffer && size > 0) {
        int length = std::min<int>(text.length(), size - 1);
        memcpy(buffer, text.data(), length);
        buffer[length] = 0;
    }
    return static_cast<int>(text.length());
}

void ob_webview_report_display_list(OBWebView* handle, unsigned char* direct, unsigned char* replayed, int stride,
    int x, int y, int width, int height)
{
    handle->view->reportDisplayList(direct, replayed, stride, IntRect(x, y, width, height));
}

void ob_webview_dirty(OBWebView* handle, int* x, int* y, int* width, int* height)
{
    IntRect dirty = handle->view->takeDirtyRect();
    *x = dirty.x();
    *y = dirty.y();
    *width = dirty.width();
    *height = dirty.height();
}

int ob_webview_dirty_rects(OBWebView* handle, int* rects, int max)
{
    IntRect dirty[8];
    unsigned count = handle->view->takeDirtyRects(dirty, static_cast<unsigned>(std::clamp(max, 0, 8)));
    for (unsigned i = 0; i < count; i++) {
        rects[i * 4] = dirty[i].x();
        rects[i * 4 + 1] = dirty[i].y();
        rects[i * 4 + 2] = dirty[i].width();
        rects[i * 4 + 3] = dirty[i].height();
    }
    return static_cast<int>(count);
}

int ob_webview_paint_laid_out(OBWebView* handle, unsigned char* argb, int stride, int x, int y, int width, int height)
{
    return handle->view->paint(argb, stride, IntRect(x, y, width, height), false);
}

int ob_webview_rendering_pending(OBWebView* handle)
{
    return handle->view->renderingUpdatePending();
}

static OptionSet<PlatformEvent::Modifier> modifiersFrom(int qualifiers)
{
    OptionSet<PlatformEvent::Modifier> modifiers;
    if (qualifiers & OB_QUAL_SHIFT)
        modifiers.add(PlatformEvent::Modifier::ShiftKey);
    if (qualifiers & OB_QUAL_CONTROL)
        modifiers.add(PlatformEvent::Modifier::ControlKey);
    if (qualifiers & OB_QUAL_ALT)
        modifiers.add(PlatformEvent::Modifier::AltKey);
    if (qualifiers & OB_QUAL_AMIGA)
        modifiers.add(PlatformEvent::Modifier::MetaKey);
    if (qualifiers & OB_QUAL_CAPSLOCK)
        modifiers.add(PlatformEvent::Modifier::CapsLockKey);
    return modifiers;
}

static LocalFrame* mainFrameOf(OBWebView* handle)
{
    return handle->view->mainFrame();
}

void ob_webview_mouse(OBWebView* handle, int type, int x, int y, int button, int qualifiers, int clickCount)
{
    RefPtr frame = mainFrameOf(handle);
    if (!frame)
        return;
    resetFPCR();
    auto modifiers = modifiersFrom(qualifiers);
    PlatformKeyboardEvent::setCurrentModifierState(modifiers);
    MouseButton mouseButton = button == OB_BUTTON_LEFT ? MouseButton::Left
        : button == OB_BUTTON_MIDDLE ? MouseButton::Middle
        : button == OB_BUTTON_RIGHT ? MouseButton::Right : MouseButton::None;
    PlatformEvent::Type eventType = type == OB_MOUSE_DOWN ? PlatformEvent::Type::MousePressed
        : type == OB_MOUSE_UP ? PlatformEvent::Type::MouseReleased : PlatformEvent::Type::MouseMoved;
    DoublePoint position(x, y);
    PlatformMouseEvent event(position, position, mouseButton, eventType, clickCount, modifiers, MonotonicTime::now(), 0,
        SyntheticClickType::NoTap, MouseEventInputSource::UserDriven);
    auto& eventHandler = frame->eventHandler();
    if (type == OB_MOUSE_DOWN)
        eventHandler.handleMousePressEvent(event);
    else if (type == OB_MOUSE_UP)
        eventHandler.handleMouseReleaseEvent(event);
    else
        eventHandler.mouseMoved(event);
}

void ob_webview_wheel(OBWebView* handle, int x, int y, int deltaX, int deltaY, int qualifiers)
{
    RefPtr frame = mainFrameOf(handle);
    if (!frame)
        return;
    resetFPCR();
    IntPoint position(x, y);
    // One wheel notch scrolls three lines, as on other systems.
    PlatformWheelEvent event(position, position, deltaX * 40.0f, deltaY * 40.0f, deltaX, deltaY,
        PlatformWheelEventGranularity::ScrollByPixelWheelEvent, qualifiers & OB_QUAL_SHIFT, qualifiers & OB_QUAL_CONTROL,
        qualifiers & OB_QUAL_ALT, qualifiers & OB_QUAL_AMIGA);
    frame->eventHandler().handleWheelEvent(event, { WheelEventProcessingSteps::SynchronousScrolling, WheelEventProcessingSteps::BlockingDOMEventDispatch });
}

// Amiga raw key codes (US positions) to Windows virtual keys and DOM codes.
struct RawKey {
    unsigned char rawKey;
    unsigned char virtualKey;
    const char* code;
    const char* key;   // for keys that make no text
};

static const RawKey rawKeys[] = {
    { 0x00, VK_OEM_3, "Backquote", nullptr }, { 0x01, '1', "Digit1", nullptr }, { 0x02, '2', "Digit2", nullptr },
    { 0x03, '3', "Digit3", nullptr }, { 0x04, '4', "Digit4", nullptr }, { 0x05, '5', "Digit5", nullptr },
    { 0x06, '6', "Digit6", nullptr }, { 0x07, '7', "Digit7", nullptr }, { 0x08, '8', "Digit8", nullptr },
    { 0x09, '9', "Digit9", nullptr }, { 0x0A, '0', "Digit0", nullptr }, { 0x0B, VK_OEM_MINUS, "Minus", nullptr },
    { 0x0C, VK_OEM_PLUS, "Equal", nullptr }, { 0x0D, VK_OEM_5, "Backslash", nullptr }, { 0x0F, VK_NUMPAD0, "Numpad0", nullptr },
    { 0x10, 'Q', "KeyQ", nullptr }, { 0x11, 'W', "KeyW", nullptr }, { 0x12, 'E', "KeyE", nullptr }, { 0x13, 'R', "KeyR", nullptr },
    { 0x14, 'T', "KeyT", nullptr }, { 0x15, 'Y', "KeyY", nullptr }, { 0x16, 'U', "KeyU", nullptr }, { 0x17, 'I', "KeyI", nullptr },
    { 0x18, 'O', "KeyO", nullptr }, { 0x19, 'P', "KeyP", nullptr }, { 0x1A, VK_OEM_4, "BracketLeft", nullptr },
    { 0x1B, VK_OEM_6, "BracketRight", nullptr }, { 0x1D, VK_NUMPAD1, "Numpad1", nullptr }, { 0x1E, VK_NUMPAD2, "Numpad2", nullptr },
    { 0x1F, VK_NUMPAD3, "Numpad3", nullptr }, { 0x20, 'A', "KeyA", nullptr }, { 0x21, 'S', "KeyS", nullptr },
    { 0x22, 'D', "KeyD", nullptr }, { 0x23, 'F', "KeyF", nullptr }, { 0x24, 'G', "KeyG", nullptr }, { 0x25, 'H', "KeyH", nullptr },
    { 0x26, 'J', "KeyJ", nullptr }, { 0x27, 'K', "KeyK", nullptr }, { 0x28, 'L', "KeyL", nullptr },
    { 0x29, VK_OEM_1, "Semicolon", nullptr }, { 0x2A, VK_OEM_7, "Quote", nullptr }, { 0x2B, VK_OEM_5, "IntlBackslash", nullptr },
    { 0x2D, VK_NUMPAD4, "Numpad4", nullptr }, { 0x2E, VK_NUMPAD5, "Numpad5", nullptr }, { 0x2F, VK_NUMPAD6, "Numpad6", nullptr },
    { 0x30, VK_OEM_102, "IntlBackslash", nullptr }, { 0x31, 'Z', "KeyZ", nullptr }, { 0x32, 'X', "KeyX", nullptr },
    { 0x33, 'C', "KeyC", nullptr }, { 0x34, 'V', "KeyV", nullptr }, { 0x35, 'B', "KeyB", nullptr }, { 0x36, 'N', "KeyN", nullptr },
    { 0x37, 'M', "KeyM", nullptr }, { 0x38, VK_OEM_COMMA, "Comma", nullptr }, { 0x39, VK_OEM_PERIOD, "Period", nullptr },
    { 0x3A, VK_OEM_2, "Slash", nullptr }, { 0x3C, VK_DECIMAL, "NumpadDecimal", nullptr },
    { 0x3D, VK_NUMPAD7, "Numpad7", nullptr }, { 0x3E, VK_NUMPAD8, "Numpad8", nullptr }, { 0x3F, VK_NUMPAD9, "Numpad9", nullptr },
    { 0x40, VK_SPACE, "Space", nullptr }, { 0x41, VK_BACK, "Backspace", "Backspace" }, { 0x42, VK_TAB, "Tab", "Tab" },
    { 0x43, VK_RETURN, "NumpadEnter", "Enter" }, { 0x44, VK_RETURN, "Enter", "Enter" }, { 0x45, VK_ESCAPE, "Escape", "Escape" },
    { 0x46, VK_DELETE, "Delete", "Delete" }, { 0x47, VK_INSERT, "Insert", "Insert" }, { 0x48, VK_PRIOR, "PageUp", "PageUp" },
    { 0x49, VK_NEXT, "PageDown", "PageDown" }, { 0x4A, VK_SUBTRACT, "NumpadSubtract", nullptr }, { 0x4B, VK_F11, "F11", "F11" },
    { 0x4C, VK_UP, "ArrowUp", "ArrowUp" }, { 0x4D, VK_DOWN, "ArrowDown", "ArrowDown" },
    { 0x4E, VK_RIGHT, "ArrowRight", "ArrowRight" }, { 0x4F, VK_LEFT, "ArrowLeft", "ArrowLeft" },
    { 0x50, VK_F1, "F1", "F1" }, { 0x51, VK_F2, "F2", "F2" }, { 0x52, VK_F3, "F3", "F3" }, { 0x53, VK_F4, "F4", "F4" },
    { 0x54, VK_F5, "F5", "F5" }, { 0x55, VK_F6, "F6", "F6" }, { 0x56, VK_F7, "F7", "F7" }, { 0x57, VK_F8, "F8", "F8" },
    { 0x58, VK_F9, "F9", "F9" }, { 0x59, VK_F10, "F10", "F10" }, { 0x5A, VK_NUMLOCK, "NumLock", "NumLock" },
    { 0x5B, VK_SCROLL, "ScrollLock", "ScrollLock" }, { 0x5C, VK_DIVIDE, "NumpadDivide", nullptr },
    { 0x5D, VK_MULTIPLY, "NumpadMultiply", nullptr }, { 0x5E, VK_ADD, "NumpadAdd", nullptr }, { 0x5F, VK_HELP, "Help", "Help" },
    { 0x60, VK_SHIFT, "ShiftLeft", "Shift" }, { 0x61, VK_SHIFT, "ShiftRight", "Shift" }, { 0x62, VK_CAPITAL, "CapsLock", "CapsLock" },
    { 0x63, VK_CONTROL, "ControlLeft", "Control" }, { 0x64, VK_MENU, "AltLeft", "Alt" }, { 0x65, VK_MENU, "AltRight", "Alt" },
    { 0x66, VK_LWIN, "MetaLeft", "Meta" }, { 0x67, VK_RWIN, "MetaRight", "Meta" }, { 0x6F, VK_F12, "F12", "F12" },
    { 0x70, VK_HOME, "Home", "Home" }, { 0x71, VK_END, "End", "End" },
};

static const RawKey* rawKeyInfo(int rawKey)
{
    for (auto& key : rawKeys) {
        if (key.rawKey == rawKey)
            return &key;
    }
    return nullptr;
}

void ob_webview_key(OBWebView* handle, int down, int rawKey, const char* text, int qualifiers)
{
    RefPtr frame = mainFrameOf(handle);
    if (!frame)
        return;
    resetFPCR();
    const RawKey* info = rawKeyInfo(rawKey);
    auto modifiers = modifiersFrom(qualifiers);
    PlatformKeyboardEvent::setCurrentModifierState(modifiers);
    String characters = String::fromUTF8(text ? text : "");
    String code = info ? String::fromLatin1(info->code) : "Unidentified"_s;
    String key = info && info->key ? String::fromLatin1(info->key) : (characters.isEmpty() ? "Unidentified"_s : characters);
    int virtualKey = info ? info->virtualKey : 0;
    // WebCore's old keyIdentifier names the keys it handles itself (Tab,
    // Backspace, arrows): "U+0009" style for characters, names otherwise.
    String keyIdentifier;
    switch (virtualKey) {
    case VK_TAB: keyIdentifier = "U+0009"_s; break;
    case VK_BACK: keyIdentifier = "U+0008"_s; break;
    case VK_ESCAPE: keyIdentifier = "U+001B"_s; break;
    case VK_DELETE: keyIdentifier = "U+007F"_s; break;
    case VK_LEFT: keyIdentifier = "Left"_s; break;
    case VK_RIGHT: keyIdentifier = "Right"_s; break;
    case VK_UP: keyIdentifier = "Up"_s; break;
    case VK_DOWN: keyIdentifier = "Down"_s; break;
    case VK_HOME: keyIdentifier = "Home"_s; break;
    case VK_END: keyIdentifier = "End"_s; break;
    case VK_PRIOR: keyIdentifier = "PageUp"_s; break;
    case VK_NEXT: keyIdentifier = "PageDown"_s; break;
    case VK_RETURN: keyIdentifier = "Enter"_s; break;
    default:
        keyIdentifier = characters.isEmpty() ? "Unidentified"_s : makeString("U+"_s, hex(characters[0], 4));
    }
    bool isKeypad = info && (String::fromLatin1(info->code).startsWith("Numpad"_s));
    PlatformKeyboardEvent event(down ? PlatformEvent::Type::KeyDown : PlatformEvent::Type::KeyUp, characters, characters,
        key, code, keyIdentifier, virtualKey, false, isKeypad, false, modifiers, MonotonicTime::now());
    frame->eventHandler().keyEvent(event);
}

void ob_webview_set_scripts(OBWebView* handle, int enabled)
{
    handle->view->setScriptsEnabled(enabled);
}

void ob_webview_set_pictures(OBWebView* handle, int enabled)
{
    handle->view->setPicturesEnabled(enabled);
}

void ob_webview_set_web_fonts(OBWebView* handle, int enabled)
{
    handle->view->setWebFontsEnabled(enabled);
}

void ob_webview_set_lite(OBWebView* handle, int enabled)
{
    handle->view->setLiteMode(enabled);
}

void ob_webview_focus(OBWebView* handle, int focused)
{
    auto& page = handle->view->page();
    page.focusController().setActive(focused);
    page.focusController().setFocused(focused);
}
