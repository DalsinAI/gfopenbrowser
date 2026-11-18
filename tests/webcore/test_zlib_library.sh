#!/bin/sh
# The engine has no zlib of its own: src/webcore/ob_zlib.c gives WebCore
# zlib's ten calls and passes each one to z.library (18 November 2026).
#
# This builds ob_zlib.c on this machine against stand-ins for exec and for
# z.library's header, and checks which library it opens, which ones it
# turns down, and that every call reaches the library with its arguments.
# It also checks that the build links no other zlib.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${OUT:-${TMPDIR:-/tmp}/ob-zlib-library.$$}
CC=${CC:-cc}
mkdir -p "$OUT/inc/exec" "$OUT/inc/proto" "$OUT/inc/libraries"
trap 'rm -rf "$OUT"' EXIT
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

cat > "$OUT/inc/exec/types.h" <<'EOF'
typedef const unsigned char *CONST_STRPTR;
typedef long LONG;
typedef unsigned long ULONG;
EOF
cat > "$OUT/inc/exec/libraries.h" <<'EOF'
struct Library { unsigned short lib_Version, lib_Revision; const char *lib_IdString; };
EOF
cat > "$OUT/inc/proto/exec.h" <<'EOF'
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
void CloseLibrary(struct Library *base);
void Forbid(void);
void Permit(void);
EOF
cat > "$OUT/inc/libraries/z.h" <<'EOF'
#define ZLIBRARY_NAME "z.library"
#define ZLIBRARY_MIN_VERSION 53
#define ZLIBRARY_GFOPENZLIB_MIN_REVISION 13
#define Z_STREAM_ERROR (-2)
#define Z_VERSION_ERROR (-6)
typedef struct { void *next_in; unsigned avail_in; void *state; } z_stream;
EOF
cat > "$OUT/inc/proto/z.h" <<'EOF'
LONG InflateInit2(z_stream *s, LONG bits);
LONG Inflate(z_stream *s, LONG flush);
LONG InflateReset(z_stream *s);
LONG InflateEnd(z_stream *s);
LONG DeflateInit2(z_stream *s, LONG level, LONG method, LONG bits, LONG mem, LONG strategy);
LONG Deflate(z_stream *s, LONG flush);
LONG DeflateReset(z_stream *s);
LONG DeflateEnd(z_stream *s);
ULONG DeflateBound(z_stream *s, ULONG length);
LONG Uncompress(unsigned char *d, ULONG *dl, const unsigned char *s, ULONG sl);
EOF

cat > "$OUT/main.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <libraries/z.h>
#include <proto/z.h>

int inflateInit2_(z_stream *, int, const char *, int);
int inflate(z_stream *, int);
int inflateReset(z_stream *);
int inflateEnd(z_stream *);
int deflateInit2_(z_stream *, int, int, int, int, int, const char *, int);
int deflate(z_stream *, int);
int deflateReset(z_stream *);
int deflateEnd(z_stream *);
unsigned long deflateBound(z_stream *, unsigned long);
int uncompress(unsigned char *, unsigned long *, const unsigned char *, unsigned long);

static struct Library lib;
static int have, opens, closes, forbids;
static char name[32];
static unsigned long wanted;
static char log_[256];

struct Library *OpenLibrary(CONST_STRPTR n, ULONG v)
{
    opens++;
    strncpy(name, (const char *)n, sizeof name - 1);
    wanted = v;
    return have && lib.lib_Version >= v ? &lib : NULL;
}
void CloseLibrary(struct Library *b) { (void)b; closes++; }
void Forbid(void) { forbids++; }
void Permit(void) { forbids--; }

#define L(text) strcat(log_, text)
#define P(call) do { int r_ = (call); printf("%d ", r_); } while (0)
LONG InflateInit2(z_stream *s, LONG b) { (void)s; L("II"); return b; }
LONG Inflate(z_stream *s, LONG f) { (void)s; L("I"); return f + 100; }
LONG InflateReset(z_stream *s) { (void)s; L("IR"); return 11; }
LONG InflateEnd(z_stream *s) { (void)s; L("IE"); return 12; }
LONG DeflateInit2(z_stream *s, LONG l, LONG m, LONG b, LONG mem, LONG st)
{ (void)s; L("DI"); return l * 10000 + m * 1000 + b * 100 + mem * 10 + st; }
LONG Deflate(z_stream *s, LONG f) { (void)s; L("D"); return f + 200; }
LONG DeflateReset(z_stream *s) { (void)s; L("DR"); return 21; }
LONG DeflateEnd(z_stream *s) { (void)s; L("DE"); return 22; }
ULONG DeflateBound(z_stream *s, ULONG n) { (void)s; L("DB"); return n + 1; }
LONG Uncompress(unsigned char *d, ULONG *dl, const unsigned char *s, ULONG sl)
{ (void)d; (void)s; L("U"); *dl = sl * 2; return 31; }

int main(int argc, char **argv)
{
    z_stream s;
    unsigned long n = 0;
    const char *mode = argc > 1 ? argv[1] : "";
    memset(&s, 0, sizeof s);
    if (!strcmp(mode, "none")) {
        /* No library: the two that open fail as a wrong version, once asked. */
        P(inflateInit2_(&s, 15, "1", (int)sizeof s)); P(deflateInit2_(&s, 6, 8, 15, 8, 0, "1", (int)sizeof s));
        P(inflate(&s, 0)); P(inflateReset(&s)); P(inflateEnd(&s));
        P(deflate(&s, 0)); P(deflateReset(&s)); P(deflateEnd(&s));
        P((int)deflateBound(&s, 1000)); P(uncompress(NULL, &n, NULL, 4));
        printf("opens=%d closes=%d log=%s name=%s want=%lu\n", opens, closes, log_, name, wanted);
        return 0;
    }
    if (!strcmp(mode, "old") || !strcmp(mode, "new") || !strcmp(mode, "other")) {
        have = 1;
        lib.lib_Version = 53;
        lib.lib_Revision = !strcmp(mode, "new") ? 13 : 12;
        lib.lib_IdString = !strcmp(mode, "other") ? "z.library 53.12" : "GFOpenZlib z.library";
        P(inflateInit2_(&s, 15, "1", (int)sizeof s));
        printf("opens=%d closes=%d log=%s\n", opens, closes, log_);
        return 0;
    }
    have = 1;
    lib.lib_Version = 54;
    lib.lib_Revision = 0;
    lib.lib_IdString = "GFOpenZlib z.library 54.0";
    /* A stream of another size is another zlib.h: refused before the library is asked. */
    P(inflateInit2_(&s, 15, "1", (int)sizeof s + 4)); P(opens);
    P(inflateInit2_(&s, -15, "1", (int)sizeof s)); P(inflate(&s, 4)); P(inflateReset(&s)); P(inflateEnd(&s));
    P(deflateInit2_(&s, 6, 8, 3, 9, 2, "1", (int)sizeof s)); P(deflate(&s, 4)); P(deflateReset(&s)); P(deflateEnd(&s));
    P((int)deflateBound(&s, 1000)); P(uncompress(NULL, &n, NULL, 4)); P((int)n);
    printf("opens=%d closes=%d forbids=%d log=%s\n", opens, closes, forbids, log_);
    return 0;
}
EOF

if "$CC" -std=gnu11 -Wall -Werror -I"$OUT/inc" -o "$OUT/t" "$ROOT/src/webcore/ob_zlib.c" "$OUT/main.c" 2> "$OUT/cc.err"; then
    pass=$((pass + 1))
    r=$("$OUT/t" none)
    ok '[ "$r" = "-6 -6 -2 -2 -2 -2 -2 -2 1170 -6 opens=1 closes=0 log= name=z.library want=53" ]' "without z.library every call fails and it is asked for once ($r)"
    r=$("$OUT/t" old)
    ok '[ "$r" = "-6 opens=1 closes=1 log=" ]' "GFOpenZlib 53.12 is turned down and closed ($r)"
    r=$("$OUT/t" new)
    ok '[ "$r" = "15 opens=1 closes=0 log=II" ]' "GFOpenZlib 53.13 is used ($r)"
    r=$("$OUT/t" other)
    ok '[ "$r" = "15 opens=1 closes=0 log=II" ]' "another maker's z.library 53.12 is used ($r)"
    r=$("$OUT/t" all)
    ok '[ "$r" = "-6 0 -15 104 11 12 68392 204 21 22 1001 31 8 opens=1 closes=0 forbids=0 log=IIIIRIEDIDDRDEDBU" ]' "each call reaches the library with its arguments ($r)"
else
    fail=$((fail + 1)); echo "FAIL: ob_zlib.c does not build on this machine"; cat "$OUT/cc.err"
fi

# The build: the forwarding file is the only zlib on the link line.
C="$ROOT/src/webcore/CMakeLists.txt"
ok 'grep -q "^add_library(OBZ STATIC ob_zlib.c)" "$C"' "CMakeLists builds OBZ from ob_zlib.c"
ok 'grep -q "OBZ oxp sqlite3" "$C"' "the programs link OBZ"
ok '! grep -Eq "(^|[ (])z oxp sqlite3" "$C"' "the programs do not link libz"
ok 'grep -q "DZLIB_LIBRARY_RELEASE=\"\$BUILD/lib/libOBZ.a\"" "$ROOT/scripts/build-webcore.sh"' "WebKit's own zlib is set to OBZ"
ok '[ "$(grep -c "inflate\|deflate\|uncompress" "$ROOT/src/webcore/ob_zlib.c")" -ge 10 ]' "ob_zlib.c has the ten calls"

echo "test_zlib_library: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
