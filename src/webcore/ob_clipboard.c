/*
 * GFOpenBrowser: the Amiga clipboard (see ob_clipboard.h). The clip's bytes
 * are made and read in plain C, tested on the host
 * (tests/test_ob_clipboard.c); the part that talks to clipboard.device is
 * built on the Amiga only.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "ob_clipboard.h"

#include <stdlib.h>
#include <string.h>

/* --- UTF-8 ----------------------------------------------------------------- */

/* The character at p (end is one past the text) and how many bytes it took.
 * A byte that does not start well-formed UTF-8 is one Latin-1 character. */
static unsigned long next(const unsigned char *p, const unsigned char *end, int *used)
{
    unsigned long c = p[0];
    int more = c >= 0xf0 && c < 0xf8 ? 3 : c >= 0xe0 && c < 0xf0 ? 2 : c >= 0xc2 && c < 0xe0 ? 1 : 0;
    static const unsigned long least[4] = { 0, 0x80, 0x800, 0x10000 };
    int i;
    *used = 1;
    if (!more || end - p <= more)
        return c;
    c &= 0x3f >> more;
    for (i = 1; i <= more; i++) {
        if ((p[i] & 0xc0) != 0x80)
            return p[0];
        c = (c << 6) | (p[i] & 0x3f);
    }
    if (c < least[more] || c > 0x10ffff || (c >= 0xd800 && c < 0xe000))
        return p[0];
    *used = more + 1;
    return c;
}

/* --- IFF --------------------------------------------------------------------- */

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static unsigned long get32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

size_t obclip_ftxt_build(const char *utf8, size_t n, unsigned char *out, size_t size)
{
    const unsigned char *text = (const unsigned char *)utf8, *end = text + n, *p;
    size_t latin = 0, wide = 0, total, at;
    int used, plain = 1;
    /* The UTF8 chunk is the text with every stray byte made a character. */
    for (p = text; p < end; p += used) {
        unsigned long c = next(p, end, &used);
        latin++;
        wide += used > 1 ? (size_t)used : c < 0x80 ? 1 : 2;
        if (c >= 0x80)
            plain = 0;
    }
    /* Plain ASCII needs no second copy. */
    total = 12 + 8 + latin + (latin & 1);
    if (!plain)
        total += 8 + wide + (wide & 1);
    if (!out || size < total)
        return total;
    memcpy(out, "FORM", 4);
    put32(out + 4, (unsigned long)(total - 8));
    memcpy(out + 8, "FTXTCHRS", 8);
    put32(out + 16, (unsigned long)latin);
    at = 20;
    for (p = text; p < end; p += used) {
        unsigned long c = next(p, end, &used);
        out[at++] = c < 0x100 ? (unsigned char)c : '?';
    }
    if (latin & 1)
        out[at++] = 0;
    if (!plain) {
        memcpy(out + at, "UTF8", 4);
        put32(out + at + 4, (unsigned long)wide);
        at += 8;
        for (p = text; p < end; p += used) {
            unsigned long c = next(p, end, &used);
            if (used > 1) {
                memcpy(out + at, p, (size_t)used);
                at += (size_t)used;
            } else if (c < 0x80)
                out[at++] = (unsigned char)c;
            else {
                out[at++] = (unsigned char)(0xc0 | (c >> 6));
                out[at++] = (unsigned char)(0x80 | (c & 0x3f));
            }
        }
        if (wide & 1)
            out[at++] = 0;
    }
    return total;
}

long obclip_ftxt_text(const unsigned char *iff, size_t n, char *out, size_t size)
{
    size_t at, end, length = 0;
    int pass, haveWide = 0;
    if (n < 12 || memcmp(iff, "FORM", 4) || memcmp(iff + 8, "FTXT", 4))
        return -1;
    end = 8 + get32(iff + 4);
    if (end > n)
        end = n;
    /* First pass: is there a UTF8 chunk? Second: the text. */
    for (pass = 0; pass < 2; pass++) {
        for (at = 12; at + 8 <= end;) {
            unsigned long chunk = get32(iff + at + 4);
            const unsigned char *data = iff + at + 8;
            if (chunk > end - at - 8)
                chunk = (unsigned long)(end - at - 8);
            if (!pass) {
                if (!memcmp(iff + at, "UTF8", 4))
                    haveWide = 1;
            } else if (haveWide && !memcmp(iff + at, "UTF8", 4)) {
                const unsigned char *p = data, *stop = data + chunk;
                int used;
                /* whole characters only, and nothing that is not UTF-8 */
                for (; p < stop && *p; p += used) {
                    unsigned long c = next(p, stop, &used);
                    if (used == 1 && c >= 0x80) {
                        if (length + 2 > size)
                            return (long)length;
                        out[length++] = (char)(0xc0 | (c >> 6));
                        out[length++] = (char)(0x80 | (c & 0x3f));
                    } else {
                        if (length + (size_t)used > size)
                            return (long)length;
                        memcpy(out + length, p, (size_t)used);
                        length += (size_t)used;
                    }
                }
                return (long)length;
            } else if (!haveWide && !memcmp(iff + at, "CHRS", 4)) {
                unsigned long i;
                for (i = 0; i < chunk && data[i]; i++) {
                    unsigned char c = data[i];
                    if (c < 0x80) {
                        if (length + 1 > size)
                            return (long)length;
                        out[length++] = (char)c;
                    } else {
                        if (length + 2 > size)
                            return (long)length;
                        out[length++] = (char)(0xc0 | (c >> 6));
                        out[length++] = (char)(0x80 | (c & 0x3f));
                    }
                }
            }
            at += 8 + chunk + (chunk & 1);
        }
    }
    return (long)length;
}

/* --- clipboard.device ------------------------------------------------------ */

#if defined(__amigaos__) || defined(AMIGA)

#include <exec/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/clipboard.h>
#include <proto/exec.h>

/* From WebCore's platform/amiga/PasteboardAmiga.cpp (WebKit patch 0121). */
typedef void (*OBPBText)(void *context, const char *utf8, unsigned long length);
typedef int (*OBPBRead)(OBPBText text, void *context);
typedef int (*OBPBWrite)(const char *utf8, unsigned long length);
extern void obpb_set_system_clipboard(OBPBRead, OBPBWrite);

/* The most a paste takes from the clipboard. */
#define CLIP_MOST (4UL * 1024 * 1024)

static struct IOClipReq *clipOpen(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOClipReq *io;
    if (!port)
        return NULL;
    io = (struct IOClipReq *)CreateIORequest(port, sizeof(struct IOClipReq));
    if (io && !OpenDevice((CONST_STRPTR)"clipboard.device", PRIMARY_CLIP, (struct IORequest *)io, 0))
        return io;
    if (io)
        DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    return NULL;
}

static void clipClose(struct IOClipReq *io)
{
    struct MsgPort *port = io->io_Message.mn_ReplyPort;
    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
}

static int clipWrite(const char *utf8, unsigned long length)
{
    size_t size = obclip_ftxt_build(utf8, length, NULL, 0);
    unsigned char *clip = malloc(size);
    struct IOClipReq *io;
    int done = 0;
    if (!clip)
        return 0;
    obclip_ftxt_build(utf8, length, clip, size);
    if ((io = clipOpen())) {
        io->io_Command = CMD_WRITE;
        io->io_Data = (STRPTR)clip;
        io->io_Length = (ULONG)size;
        io->io_Offset = 0;
        io->io_ClipID = 0;
        done = !DoIO((struct IORequest *)io) && io->io_Actual == (ULONG)size;
        /* the write is shown to the others, also when it was cut short */
        io->io_Command = CMD_UPDATE;
        DoIO((struct IORequest *)io);
        clipClose(io);
    }
    free(clip);
    return done;
}

static int clipRead(OBPBText text, void *context)
{
    struct IOClipReq *io = clipOpen();
    unsigned char head[12], rest[64];
    unsigned char *clip = NULL;
    unsigned long size = 0;
    int done = 0;
    if (!io)
        return 0;
    io->io_Command = CMD_READ;
    io->io_Data = (STRPTR)head;
    io->io_Length = sizeof head;
    io->io_Offset = 0;
    io->io_ClipID = 0;
    if (!DoIO((struct IORequest *)io) && io->io_Actual == sizeof head && !memcmp(head, "FORM", 4) && !memcmp(head + 8, "FTXT", 4)) {
        size = 8 + get32(head + 4);
        if (size > sizeof head && size <= CLIP_MOST && (clip = malloc(size))) {
            memcpy(clip, head, sizeof head);
            io->io_Command = CMD_READ;
            io->io_Data = (STRPTR)(clip + sizeof head);
            io->io_Length = size - sizeof head;
            if (DoIO((struct IORequest *)io) || io->io_Actual != size - sizeof head) {
                free(clip);
                clip = NULL;
            }
        }
    }
    /* A read past the clip's end tells the device this reader is done. */
    do {
        io->io_Command = CMD_READ;
        io->io_Data = (STRPTR)rest;
        io->io_Length = sizeof rest;
    } while (!DoIO((struct IORequest *)io) && io->io_Actual);
    clipClose(io);
    if (clip) {
        /* Latin-1 grows to twice its size at most; UTF-8 stays as it is */
        char *utf8 = malloc(2 * (size_t)size + 1);
        long length = utf8 ? obclip_ftxt_text(clip, size, utf8, 2 * (size_t)size) : -1;
        if (length >= 0) {
            text(context, utf8, (unsigned long)length);
            done = 1;
        }
        free(utf8);
        free(clip);
    }
    return done;
}

void obclip_install(void)
{
    obpb_set_system_clipboard(clipRead, clipWrite);
}

#endif
