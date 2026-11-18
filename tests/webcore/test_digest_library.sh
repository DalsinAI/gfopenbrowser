#!/bin/sh
# The engine has no SHA code of its own: WebKit's CryptoDigest on the Amiga
# (patch 0124) calls src/webcore/ob_digest.c, which has gfopencrypto.library
# work out every block (18 November 2026).
#
# This builds ob_digest.c on this machine against stand-ins for exec and
# for the library's headers. The stand-in library does what the real one
# does with whole blocks (FIPS 180-4), so the hashes that come out can be
# compared with this machine's sha1sum, sha256sum, sha384sum and sha512sum:
# that checks the part ob_digest.c keeps, the buffering and the padding, at
# every length around a block's end and with the input cut in pieces.
# It also checks which library is opened and which is turned down, and that
# the patch leaves no round function in WebKit.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${OUT:-${TMPDIR:-/tmp}/ob-digest-library.$$}
CC=${CC:-cc}
mkdir -p "$OUT/inc/exec" "$OUT/inc/proto" "$OUT/inc/libraries"
trap 'rm -rf "$OUT"' EXIT
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

cat > "$OUT/inc/exec/types.h" <<'EOF'
typedef const unsigned char *CONST_STRPTR;
typedef int LONG;
typedef unsigned int ULONG;
typedef unsigned char UBYTE;
EOF
cat > "$OUT/inc/exec/libraries.h" <<'EOF'
struct Library { unsigned short lib_Version, lib_Revision; };
EOF
cat > "$OUT/inc/proto/exec.h" <<'EOF'
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
void CloseLibrary(struct Library *base);
void Forbid(void);
void Permit(void);
EOF
cat > "$OUT/inc/libraries/gfopencrypto.h" <<'EOF'
#define OPENCRYPTOLIB_NAME "gfopencrypto.library"
struct OCBlocksRequest { UBYTE *state; const void *data; ULONG blocks; };
#define OC_SHA1_IV { 0x67,0x45,0x23,0x01, 0xef,0xcd,0xab,0x89, \
                     0x98,0xba,0xdc,0xfe, 0x10,0x32,0x54,0x76, \
                     0xc3,0xd2,0xe1,0xf0 }
EOF
cat > "$OUT/inc/proto/gfopencrypto.h" <<'EOF'
extern struct Library *OpenCryptoBase;
LONG OC_SHA1Blocks(struct OCBlocksRequest *request);
LONG OC_SHA256Blocks(struct OCBlocksRequest *request);
LONG OC_SHA512Blocks(struct OCBlocksRequest *request);
EOF

cat > "$OUT/main.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <libraries/gfopencrypto.h>
#include <proto/gfopencrypto.h>
#include "ob_digest.h"

static struct Library lib;
static int have, opens, closes, forbids, refuse;
static unsigned long calls, blocksSeen, wanted;
static char name[32];

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

/* The stand-in library: whole blocks onto a big-endian chaining value. */
static const uint64_t K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};
static uint64_t get(const UBYTE *p, int n) { uint64_t v = 0; while (n--) v = v << 8 | *p++; return v; }
static void put(UBYTE *p, int n, uint64_t v) { while (n--) { p[n] = (UBYTE)v; v >>= 8; } }
static uint32_t r32(uint32_t x, int n) { return x >> n | x << (32 - n); }
static uint64_t r64(uint64_t x, int n) { return x >> n | x << (64 - n); }

static int seen(struct OCBlocksRequest *q)
{
    calls++;
    blocksSeen += q->blocks;
    return refuse && calls >= (unsigned long)refuse;
}

LONG OC_SHA1Blocks(struct OCBlocksRequest *q)
{
    const UBYTE *p = (const UBYTE *)q->data;
    ULONG n;
    int i;
    if (seen(q)) return -2;
    for (n = 0; n < q->blocks; n++, p += 64) {
        uint32_t w[80], s[5], a, b, c, d, e, f, t;
        for (i = 0; i < 5; i++) s[i] = (uint32_t)get(q->state + 4 * i, 4);
        for (i = 0; i < 16; i++) w[i] = (uint32_t)get(p + 4 * i, 4);
        for (; i < 80; i++) w[i] = r32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 31);
        a = s[0]; b = s[1]; c = s[2]; d = s[3]; e = s[4];
        for (i = 0; i < 80; i++) {
            if (i < 20) f = ((b & c) | (~b & d)) + 0x5a827999;
            else if (i < 40) f = (b ^ c ^ d) + 0x6ed9eba1;
            else if (i < 60) f = ((b & c) | (b & d) | (c & d)) + 0x8f1bbcdc;
            else f = (b ^ c ^ d) + 0xca62c1d6;
            t = r32(a, 27) + f + e + w[i];
            e = d; d = c; c = r32(b, 2); b = a; a = t;
        }
        s[0] += a; s[1] += b; s[2] += c; s[3] += d; s[4] += e;
        for (i = 0; i < 5; i++) put(q->state + 4 * i, 4, s[i]);
    }
    return 0;
}

LONG OC_SHA256Blocks(struct OCBlocksRequest *q)
{
    const UBYTE *p = (const UBYTE *)q->data;
    ULONG n;
    int i;
    if (seen(q)) return -2;
    for (n = 0; n < q->blocks; n++, p += 64) {
        uint32_t w[64], s[8], v[8], t1, t2;
        for (i = 0; i < 8; i++) v[i] = s[i] = (uint32_t)get(q->state + 4 * i, 4);
        for (i = 0; i < 16; i++) w[i] = (uint32_t)get(p + 4 * i, 4);
        for (; i < 64; i++)
            w[i] = w[i - 16] + (r32(w[i - 15], 7) ^ r32(w[i - 15], 18) ^ w[i - 15] >> 3)
                + w[i - 7] + (r32(w[i - 2], 17) ^ r32(w[i - 2], 19) ^ w[i - 2] >> 10);
        for (i = 0; i < 64; i++) {
            /* SHA-256's constants are the top halves of SHA-512's first 64 */
            t1 = v[7] + (r32(v[4], 6) ^ r32(v[4], 11) ^ r32(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6]))
                + (uint32_t)(K[i] >> 32) + w[i];
            t2 = (r32(v[0], 2) ^ r32(v[0], 13) ^ r32(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
            v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1; v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
        }
        for (i = 0; i < 8; i++) put(q->state + 4 * i, 4, s[i] + v[i]);
    }
    return 0;
}

LONG OC_SHA512Blocks(struct OCBlocksRequest *q)
{
    const UBYTE *p = (const UBYTE *)q->data;
    ULONG n;
    int i;
    if (seen(q)) return -2;
    for (n = 0; n < q->blocks; n++, p += 128) {
        uint64_t w[80], s[8], v[8], t1, t2;
        for (i = 0; i < 8; i++) v[i] = s[i] = get(q->state + 8 * i, 8);
        for (i = 0; i < 16; i++) w[i] = get(p + 8 * i, 8);
        for (; i < 80; i++)
            w[i] = w[i - 16] + (r64(w[i - 15], 1) ^ r64(w[i - 15], 8) ^ w[i - 15] >> 7)
                + w[i - 7] + (r64(w[i - 2], 19) ^ r64(w[i - 2], 61) ^ w[i - 2] >> 6);
        for (i = 0; i < 80; i++) {
            t1 = v[7] + (r64(v[4], 14) ^ r64(v[4], 18) ^ r64(v[4], 41)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K[i] + w[i];
            t2 = (r64(v[0], 28) ^ r64(v[0], 34) ^ r64(v[0], 39)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
            v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1; v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
        }
        for (i = 0; i < 8; i++) put(q->state + 8 * i, 8, s[i] + v[i]);
    }
    return 0;
}

/* hash KIND CHUNK < data: the hash of standard input, given CHUNK bytes at a time
 * open MODE: what ob_digest_new() does with no library, an old one, a refusal */
int main(int argc, char **argv)
{
    static unsigned char data[70000];
    unsigned char out[64];
    size_t n, at, chunk;
    void *d;
    int length, i;
    if (argc > 2 && !strcmp(argv[1], "open")) {
        const char *mode = argv[2];
        void *first, *second;
        have = strcmp(mode, "none") != 0;
        lib.lib_Version = !strcmp(mode, "v3") ? 3 : 2;
        lib.lib_Revision = !strcmp(mode, "old") ? 1 : !strcmp(mode, "v3") ? 0 : 2;
        refuse = !strcmp(mode, "refuse") ? 2 : 0;
        first = ob_digest_new(OB_DIGEST_SHA256);
        second = ob_digest_new(OB_DIGEST_SHA512);
        printf("%d %d ", first != NULL, second != NULL);
        printf("%d %d ", ob_digest_new(0) != NULL, ob_digest_new(5) != NULL);
        ob_digest_update(first, data, 200);          /* three blocks in one call, then a refusal */
        ob_digest_update(first, data, 200);
        printf("%d ", ob_digest_final(first, out));
        printf("%d ", ob_digest_final(first, out));  /* one hash for each digest */
        ob_digest_update(NULL, data, 1);
        ob_digest_free(first);
        ob_digest_free(second);
        ob_digest_free(NULL);
        printf("opens=%d closes=%d forbids=%d calls=%lu blocks=%lu name=%s want=%lu\n",
            opens, closes, forbids, calls, blocksSeen, name, wanted);
        return 0;
    }
    if (argc < 4 || strcmp(argv[1], "hash"))
        return 2;
    have = 1;
    lib.lib_Version = 2;
    lib.lib_Revision = 4;
    n = fread(data, 1, sizeof data, stdin);
    chunk = (size_t)atoi(argv[3]);
    d = ob_digest_new(atoi(argv[2]));
    if (!d)
        return 3;
    for (at = 0; at < n; at += chunk)
        ob_digest_update(d, data + at, n - at < chunk ? n - at : chunk);
    length = ob_digest_final(d, out);
    ob_digest_free(d);
    for (i = 0; i < length; i++)
        printf("%02x", out[i]);
    printf("\n");
    return length > 0 ? 0 : 4;
}
EOF

S="$ROOT/src/webcore"
if "$CC" -std=gnu11 -Wall -Werror -I"$OUT/inc" -I"$S" -o "$OUT/t" "$S/ob_digest.c" "$OUT/main.c" 2> "$OUT/cc.err"; then
    pass=$((pass + 1))
    # 66,000 bytes that do not repeat on a block's length.
    awk 'BEGIN { for (i = 0; i < 6000; i++) printf "%05d:%04x;", i * 7919 % 100000, i * 31 % 65536 }' > "$OUT/data"
    bad=""
    count=0
    for kind in 1 2 3 4; do
        case $kind in 1) tool=sha1sum ;; 2) tool=sha256sum ;; 3) tool=sha384sum ;; 4) tool=sha512sum ;; esac
        for n in 0 1 3 54 55 56 57 63 64 65 110 111 112 113 119 120 127 128 129 191 192 193 255 256 257 1000 4096 65537; do
            head -c "$n" "$OUT/data" > "$OUT/in"
            want=$("$tool" < "$OUT/in" | cut -d' ' -f1)
            for chunk in 1 7 64 100 128 4096 70000; do
                # one byte at a time only for the short ones
                if [ "$chunk" -eq 1 ] && [ "$n" -gt 1000 ]; then continue; fi
                got=$("$OUT/t" hash "$kind" "$chunk" < "$OUT/in" || true)
                count=$((count + 1))
                [ "$got" = "$want" ] || bad="$bad $tool:$n/$chunk"
            done
        done
    done
    ok '[ -z "$bad" ] && [ "$count" -gt 700 ]' "the hashes are this machine's own, $count compared (wrong:$bad)"
    r=$(printf abc | "$OUT/t" hash 2 70000)
    ok '[ "$r" = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ]' "SHA-256 of abc ($r)"
    r=$(printf abc | "$OUT/t" hash 1 70000)
    ok '[ "$r" = "a9993e364706816aba3e25717850c26c9cd0d89d" ]' "SHA-1 of abc ($r)"
    r=$("$OUT/t" open none)
    ok '[ "$r" = "0 0 0 0 -1 -1 opens=1 closes=0 forbids=0 calls=0 blocks=0 name=gfopencrypto.library want=2" ]' "without the library there is no digest and it is asked for once ($r)"
    r=$("$OUT/t" open old)
    ok '[ "$r" = "0 0 0 0 -1 -1 opens=1 closes=1 forbids=0 calls=0 blocks=0 name=gfopencrypto.library want=2" ]' "gfopencrypto.library 2.1 (no SHA-1 yet) is turned down and closed ($r)"
    r=$("$OUT/t" open new)
    ok '[ "$r" = "1 1 0 0 32 -1 opens=1 closes=0 forbids=0 calls=4 blocks=7 name=gfopencrypto.library want=2" ]' "2.2 is used: opened once, and whole blocks go in one call ($r)"
    r=$("$OUT/t" open v3)
    ok '[ "${r%% opens*}" = "1 1 0 0 32 -1" ]' "version 3.0 is used ($r)"
    r=$("$OUT/t" open refuse)
    ok '[ "${r%% opens*}" = "1 1 0 0 -1 -1" ]' "a call the library refuses gives no hash ($r)"
else
    fail=$((fail + 1)); echo "FAIL: ob_digest.c does not build on this machine"; cat "$OUT/cc.err"
fi

# The build, and what is left in WebKit.
C="$S/CMakeLists.txt"
P="$ROOT/webkit/patches/0124-amiga-digests-from-gfopencrypto-library.patch"
ok 'grep -q "^    ob_digest.c$" "$C"' "CMakeLists builds ob_digest.c into OBCore"
ok 'grep -q "SOURCE ob_digest.c APPEND PROPERTY" "$C" && grep -A1 "SOURCE ob_digest.c APPEND PROPERTY" "$C" | grep -q "OB_GFOPENTLS_DIR}/include"' "ob_digest.c gets the library's headers from the GFOpenTLS checkout"
ok '[ -f "$P" ]' "patch 0124 is there"
if [ -f "$P" ]; then
    ok '[ "$(grep -c "^+.*ob_digest_\(new\|update\|final\|free\)(" "$P")" -ge 8 ]' "the patch declares and calls the four functions"
    ok '! grep -q "^+.*\(rotr32\|rotr64\|0x428a2f98\|wtf/SHA1.h\)" "$P"' "the patch adds no round function, constant or WTF SHA-1"
    ok '[ "$(grep -c "^-.*\(rotr32\|rotr64\)" "$P")" -ge 10 ]' "the patch takes the round functions out"
    # each hunk has the lines its header says
    bad=$(awk '/^@@ / { if (c && (o || n)) print "x"; split($2, a, ","); split($3, b, ","); o = a[2]; n = b[2]; c = 1; next }
        /^-- $/ { if (c && (o || n)) print "x"; c = 0 }
        c && /^ / { o--; n-- } c && /^-/ { o-- } c && /^\+/ { n-- }' "$P")
    ok '[ -z "$bad" ]' "each hunk counts its lines"
    ok 'grep -q "0124-amiga-digests-from-gfopencrypto-library.patch" "$ROOT/webkit/README.md"' "webkit/README.md lists the patch"
fi
ok '! grep -q "rotr\|0x428a2f98" "$S/ob_digest.c"' "ob_digest.c has no round function either"
ok 'grep -q "OC_SHA1Blocks" "$S/ob_digest.c" && grep -q "OC_SHA256Blocks" "$S/ob_digest.c" && grep -q "OC_SHA512Blocks" "$S/ob_digest.c"' "ob_digest.c calls the library's three block functions"

echo "test_digest_library: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
