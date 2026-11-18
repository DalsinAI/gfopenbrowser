/*
 * GFOpenBrowser: zlib's calls, answered by z.library (GFOpenZlib, which
 * GFOpenUp installs in LIBS:). WebCore calls ten of zlib's functions
 * (CompressionStream and DecompressionStream, and the fonts and files that
 * are deflated); this file is the whole of "libz" the engine links, so no
 * inflate or deflate code is in the engine itself.
 *
 * z.library is opened at the first call and closed when the program ends.
 * Without it (or with a GFOpenZlib older than 53.13, whose table differs)
 * every call fails as zlib does for a wrong version, and the page gets the
 * error WebCore gives for a stream that cannot be opened.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>

struct Library *ZBase;
#include <libraries/z.h>
#include <proto/z.h>

static int tried;

static int zlibOpen(void)
{
    struct Library *base;
    if (ZBase)
        return 1;
    if (tried)
        return 0;
    base = OpenLibrary((CONST_STRPTR)ZLIBRARY_NAME, ZLIBRARY_MIN_VERSION);
    if (base && base->lib_Version == ZLIBRARY_MIN_VERSION
        && base->lib_Revision < ZLIBRARY_GFOPENZLIB_MIN_REVISION
        && base->lib_IdString && strstr((const char *)base->lib_IdString, "GFOpenZlib")) {
        CloseLibrary(base);
        base = NULL;
    }
    /* Two threads may get here together: the second one's count is given
     * back, and both use the same base. */
    Forbid();
    if (!ZBase) {
        ZBase = base;
        base = NULL;
    }
    tried = 1;
    Permit();
    if (base)
        CloseLibrary(base);
    return ZBase != NULL;
}

static void __attribute__((destructor)) zlibClose(void)
{
    if (ZBase) {
        CloseLibrary(ZBase);
        ZBase = NULL;
    }
}

/* zlib.h's names and C arguments. The z_stream is zlib.h's own layout
 * (libraries/z.h), so WebCore's streams pass straight through. */

int inflateInit2_(z_stream *stream, int windowBits, const char *version, int streamSize)
{
    (void)version;
    if (streamSize != (int)sizeof(z_stream) || !zlibOpen())
        return Z_VERSION_ERROR;
    return (int)InflateInit2(stream, windowBits);
}

int inflate(z_stream *stream, int flush)
{
    return ZBase ? (int)Inflate(stream, flush) : Z_STREAM_ERROR;
}

int inflateReset(z_stream *stream)
{
    return ZBase ? (int)InflateReset(stream) : Z_STREAM_ERROR;
}

int inflateEnd(z_stream *stream)
{
    return ZBase ? (int)InflateEnd(stream) : Z_STREAM_ERROR;
}

int deflateInit2_(z_stream *stream, int level, int method, int windowBits, int memLevel,
    int strategy, const char *version, int streamSize)
{
    (void)version;
    if (streamSize != (int)sizeof(z_stream) || !zlibOpen())
        return Z_VERSION_ERROR;
    return (int)DeflateInit2(stream, level, method, windowBits, memLevel, strategy);
}

int deflate(z_stream *stream, int flush)
{
    return ZBase ? (int)Deflate(stream, flush) : Z_STREAM_ERROR;
}

int deflateReset(z_stream *stream)
{
    return ZBase ? (int)DeflateReset(stream) : Z_STREAM_ERROR;
}

int deflateEnd(z_stream *stream)
{
    return ZBase ? (int)DeflateEnd(stream) : Z_STREAM_ERROR;
}

unsigned long deflateBound(z_stream *stream, unsigned long sourceLength)
{
    if (ZBase)
        return (unsigned long)DeflateBound(stream, sourceLength);
    /* zlib's own answer for a stream it knows nothing about */
    return sourceLength + ((sourceLength + 7) >> 3) + ((sourceLength + 63) >> 6) + 5 + 6 + 18;
}

int uncompress(unsigned char *dest, unsigned long *destLength, const unsigned char *source,
    unsigned long sourceLength)
{
    if (!zlibOpen())
        return Z_VERSION_ERROR;
    return (int)Uncompress(dest, destLength, source, sourceLength);
}
