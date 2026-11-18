/*
 * GFOpenBrowser: SHA-1, SHA-256, SHA-384 and SHA-512 for the engine, worked
 * out by gfopencrypto.library (GFOpenTLS's, which GFOpenUp installs in LIBS:
 * and the package carries). WebKit's PAL CryptoDigest on the Amiga
 * (CryptoDigestAmiga.cpp, WebKit patch 0124) calls the four functions below
 * and has no hash code of its own any more.
 *
 * The library takes whole blocks and a chaining value (OC_SHA1Blocks,
 * OC_SHA256Blocks, OC_SHA512Blocks; SHA-384 is SHA-512 from another start).
 * What is left here is the bookkeeping every caller of those needs: the
 * part of a block not yet full, the length, and the padding (FIPS 180-4,
 * 5.1).
 *
 * gfopencrypto.library is opened at the first digest and closed when the
 * program ends; it must be 2.2 or newer (SHA-1 came with that revision).
 * Without it ob_digest_new() gives NULL and the caller gets no hash.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>

struct Library *OpenCryptoBase;
#include <libraries/gfopencrypto.h>
#include <proto/gfopencrypto.h>

#include "ob_digest.h"

struct OBDigest {
    UBYTE state[64];             /* the chaining value, big-endian */
    UBYTE block[128];            /* the part of a block not yet full */
    unsigned long long length;   /* bytes taken so far */
    unsigned used;               /* bytes in block[] */
    unsigned blockSize;          /* 64, or 128 for SHA-384 and SHA-512 */
    unsigned digestLength;
    int kind;
    int failed;                  /* the library refused a call */
};

static const UBYTE sha1Start[20] = OC_SHA1_IV;
static const UBYTE sha256Start[32] = {
    0x6a, 0x09, 0xe6, 0x67, 0xbb, 0x67, 0xae, 0x85, 0x3c, 0x6e, 0xf3, 0x72, 0xa5, 0x4f, 0xf5, 0x3a,
    0x51, 0x0e, 0x52, 0x7f, 0x9b, 0x05, 0x68, 0x8c, 0x1f, 0x83, 0xd9, 0xab, 0x5b, 0xe0, 0xcd, 0x19
};
static const UBYTE sha384Start[64] = {
    0xcb, 0xbb, 0x9d, 0x5d, 0xc1, 0x05, 0x9e, 0xd8, 0x62, 0x9a, 0x29, 0x2a, 0x36, 0x7c, 0xd5, 0x07,
    0x91, 0x59, 0x01, 0x5a, 0x30, 0x70, 0xdd, 0x17, 0x15, 0x2f, 0xec, 0xd8, 0xf7, 0x0e, 0x59, 0x39,
    0x67, 0x33, 0x26, 0x67, 0xff, 0xc0, 0x0b, 0x31, 0x8e, 0xb4, 0x4a, 0x87, 0x68, 0x58, 0x15, 0x11,
    0xdb, 0x0c, 0x2e, 0x0d, 0x64, 0xf9, 0x8f, 0xa7, 0x47, 0xb5, 0x48, 0x1d, 0xbe, 0xfa, 0x4f, 0xa4
};
static const UBYTE sha512Start[64] = {
    0x6a, 0x09, 0xe6, 0x67, 0xf3, 0xbc, 0xc9, 0x08, 0xbb, 0x67, 0xae, 0x85, 0x84, 0xca, 0xa7, 0x3b,
    0x3c, 0x6e, 0xf3, 0x72, 0xfe, 0x94, 0xf8, 0x2b, 0xa5, 0x4f, 0xf5, 0x3a, 0x5f, 0x1d, 0x36, 0xf1,
    0x51, 0x0e, 0x52, 0x7f, 0xad, 0xe6, 0x82, 0xd1, 0x9b, 0x05, 0x68, 0x8c, 0x2b, 0x3e, 0x6c, 0x1f,
    0x1f, 0x83, 0xd9, 0xab, 0xfb, 0x41, 0xbd, 0x6b, 0x5b, 0xe0, 0xcd, 0x19, 0x13, 0x7e, 0x21, 0x79
};

static int tried;

static int cryptoOpen(void)
{
    struct Library *base;
    if (OpenCryptoBase)
        return 1;
    if (tried)
        return 0;
    base = OpenLibrary((CONST_STRPTR)OPENCRYPTOLIB_NAME, 2);
    if (base && base->lib_Version == 2 && base->lib_Revision < 2) {
        CloseLibrary(base);
        base = NULL;
    }
    /* Two threads may get here together: the second one's count is given
     * back, and both use the same base. */
    Forbid();
    if (!OpenCryptoBase) {
        OpenCryptoBase = base;
        base = NULL;
    }
    tried = 1;
    Permit();
    if (base)
        CloseLibrary(base);
    return OpenCryptoBase != NULL;
}

static void __attribute__((destructor)) cryptoClose(void)
{
    if (OpenCryptoBase) {
        CloseLibrary(OpenCryptoBase);
        OpenCryptoBase = NULL;
    }
}

static void run(struct OBDigest *d, const void *data, unsigned long blocks)
{
    struct OCBlocksRequest request;
    LONG rc;
    request.state = d->state;
    request.data = data;
    request.blocks = blocks;
    if (d->kind == OB_DIGEST_SHA1)
        rc = OC_SHA1Blocks(&request);
    else if (d->kind == OB_DIGEST_SHA256)
        rc = OC_SHA256Blocks(&request);
    else
        rc = OC_SHA512Blocks(&request);
    if (rc < 0)
        d->failed = 1;
}

void *ob_digest_new(int kind)
{
    struct OBDigest *d;
    const UBYTE *start;
    unsigned stateLength;
    if (kind < OB_DIGEST_SHA1 || kind > OB_DIGEST_SHA512 || !cryptoOpen())
        return NULL;
    d = (struct OBDigest *)calloc(1, sizeof *d);
    if (!d)
        return NULL;
    d->kind = kind;
    d->blockSize = 64;
    switch (kind) {
    case OB_DIGEST_SHA1:
        start = sha1Start; stateLength = 20; d->digestLength = 20;
        break;
    case OB_DIGEST_SHA256:
        start = sha256Start; stateLength = 32; d->digestLength = 32;
        break;
    case OB_DIGEST_SHA384:
        start = sha384Start; stateLength = 64; d->digestLength = 48; d->blockSize = 128;
        break;
    default:
        start = sha512Start; stateLength = 64; d->digestLength = 64; d->blockSize = 128;
        break;
    }
    memcpy(d->state, start, stateLength);
    return d;
}

void ob_digest_update(void *digest, const void *data, unsigned long length)
{
    struct OBDigest *d = (struct OBDigest *)digest;
    const UBYTE *p = (const UBYTE *)data;
    unsigned long n;
    if (!d || d->failed || !length)
        return;
    d->length += length;
    if (d->used) {
        n = d->blockSize - d->used;
        if (n > length)
            n = length;
        memcpy(d->block + d->used, p, n);
        d->used += n;
        p += n;
        length -= n;
        if (d->used < d->blockSize)
            return;
        run(d, d->block, 1);
        d->used = 0;
    }
    n = length / d->blockSize;
    if (n) {
        run(d, p, n);
        p += n * d->blockSize;
        length -= n * d->blockSize;
    }
    if (length) {
        memcpy(d->block, p, length);
        d->used = length;
    }
}

int ob_digest_final(void *digest, unsigned char *out)
{
    struct OBDigest *d = (struct OBDigest *)digest;
    unsigned lengthBytes, i;
    unsigned long long bits;
    if (!d || d->failed)
        return -1;
    lengthBytes = d->blockSize == 128 ? 16 : 8;
    d->block[d->used++] = 0x80;
    if (d->used > d->blockSize - lengthBytes) {
        memset(d->block + d->used, 0, d->blockSize - d->used);
        run(d, d->block, 1);
        d->used = 0;
    }
    memset(d->block + d->used, 0, d->blockSize - d->used);
    /* the length in bits, big-endian, at the end of the block; of SHA-512's
     * 128 bits only the three that a 64-bit count of bytes spills over */
    bits = d->length << 3;
    if (lengthBytes == 16)
        d->block[d->blockSize - 9] = (UBYTE)(d->length >> 61);
    for (i = 0; i < 8; i++)
        d->block[d->blockSize - 1 - i] = (UBYTE)(bits >> (8 * i));
    run(d, d->block, 1);
    d->used = 0;
    if (d->failed)
        return -1;
    memcpy(out, d->state, d->digestLength);
    d->failed = 1;               /* one hash for each digest */
    return (int)d->digestLength;
}

void ob_digest_free(void *digest)
{
    free(digest);
}
