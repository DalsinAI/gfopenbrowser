// The OXP bridge on the host: a receiver with XMLDocumentParser's SAX
// methods prints what it is given, in the format of gfopenxml's
// tests/ref/libxml2dump (libxml2's own events), so the two can be compared.
// It reads the arrays exactly as XMLDocumentParser does: namespaces as
// prefix/URI pairs, attributes as five pointers with the value's end.
//   bridgetest [-f] [-x] FILE    -f: fragment (as WebKit's fragment parser)
//                                -x: resolve 'nbsp' as an XHTML document would
// Copyright (C) 2026 AmiCradle Team. BSD-2-Clause, as the bridge.
#include "XMLDocumentParserOXPBridge.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace WebCore::OXPBridge;

struct Receiver {
    std::string pending;
    bool xhtml { false };
    bool resolveNbsp { false };
    int defaultedSeen { 0 };
    void flush()
    {
        if (pending.empty())
            return;
        std::fputs("T ", stdout);
        esc(pending.data(), pending.size());
        std::putchar('\n');
        pending.clear();
    }
    static void esc(const char* s, size_t n)
    {
        for (size_t i = 0; i < n; ++i) {
            unsigned char c = s[i];
            if (c == '\n') std::fputs("\\n", stdout);
            else if (c == '\t') std::fputs("\\t", stdout);
            else if (c == '\\') std::fputs("\\\\", stdout);
            else if (c < 0x20) std::printf("\\x%02x", c);
            else std::putchar(c);
        }
    }
    static const char* s(const Char* p) { return reinterpret_cast<const char*>(p); }
    static void name(const Char* uri, const Char* local)
    {
        if (uri) std::printf("{%s}", s(uri));
        std::fputs(s(local), stdout);
    }
    void startElementNs(const Char* local, const Char*, const Char* uri, int numNamespaces, const Char** namespaces,
        int numAttributes, int numDefaulted, const Char** attributes)
    {
        flush();
        std::fputs("S ", stdout); name(uri, local); std::putchar('\n');
        for (int i = 0; i < numNamespaces; ++i)
            std::printf("N %s=%s\n", namespaces[2 * i] ? s(namespaces[2 * i]) : "", namespaces[2 * i + 1] ? s(namespaces[2 * i + 1]) : "");
        for (int i = 0; i < numAttributes; ++i) {
            const Char** a = attributes + 5 * i;
            std::fputs("A ", stdout); name(a[2], a[0]); std::putchar('=');
            esc(s(a[3]), static_cast<size_t>(a[4] - a[3]));
            std::putchar('\n');
        }
        defaultedSeen += numDefaulted;
    }
    void endElementNs() { flush(); std::puts("E ?"); }
    void characters(std::span<const Char> t) { pending.append(s(t.data()), t.size()); }
    void cdataBlock(std::span<const Char> t) { pending.append(s(t.data()), t.size()); }
    void comment(const Char* t) { flush(); std::fputs("C ", stdout); esc(s(t), std::strlen(s(t))); std::putchar('\n'); }
    void processingInstruction(const Char* target, const Char* data)
    {
        flush(); std::printf("P %s ", s(target)); esc(s(data), std::strlen(s(data))); std::putchar('\n');
    }
    void startDocument(const Char* version, const Char* encoding, int standalone)
    {
        std::fprintf(stderr, "startDocument version=%s encoding=%s standalone=%d\n", version ? s(version) : "(null)", encoding ? s(encoding) : "(null)", standalone);
    }
    void endDocument() { flush(); std::puts("END"); }
    void internalSubset(const Char* n, const Char* pub, const Char* sys)
    {
        flush(); std::printf("DOCTYPE %s %s %s\n", s(n), pub ? s(pub) : "-", sys ? s(sys) : "-");
    }
    void setIsXHTMLDocument(bool x) { xhtml = x; std::fprintf(stderr, "setIsXHTMLDocument %d\n", x); }
    const char* resolveEntity(const char* n) { return (resolveNbsp && xhtml && !std::strcmp(n, "nbsp")) ? "\xC2\xA0" : nullptr; }
    void reportError(ErrorType t, const char* m, unsigned long line, unsigned long col)
    {
        if (t == ErrorType::Warning) return;
        flush();
        std::printf("ERROR %d %lu:%lu\n", t == ErrorType::Fatal ? 2 : 1, line, col);
        std::fprintf(stderr, "error: %s\n", m);
    }
};

// Counts the guards made, to check every callback is covered.
static int guardsMade;
struct CountingGuard {
    explicit CountingGuard(Receiver&) { ++guardsMade; }
};

int main(int argc, char** argv)
{
    unsigned options = OXP_OPT_UNBOUND_PREFIX;
    bool nbsp = false;
    int i = 1;
    for (; i < argc - 1; ++i) {
        if (!std::strcmp(argv[i], "-f")) options |= OXP_OPT_FRAGMENT;
        else if (!std::strcmp(argv[i], "-x")) nbsp = true;
    }
    if (i >= argc) return 2;
    FILE* f = std::fopen(argv[i], "rb");
    if (!f) return 2;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::rewind(f);
    std::string data(static_cast<size_t>(n), '\0');
    if (std::fread(data.data(), 1, data.size(), f) != data.size()) return 2;
    std::fclose(f);
    Receiver receiver;
    receiver.resolveNbsp = nbsp;
    Bridge<Receiver, CountingGuard> bridge(receiver);
    oxp_parser* p = oxp_new(&Bridge<Receiver, CountingGuard>::handler(), &bridge, nullptr);
    oxp_set_options(p, options);
    oxp_code c = oxp_parse(p, data.data(), data.size(), 1);
    receiver.flush();
    std::fprintf(stderr, "defaulted %d guards %d\n", receiver.defaultedSeen, guardsMade);
    oxp_free(p);
    return c ? 1 : 0;
}
