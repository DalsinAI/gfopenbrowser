/*
 * GFOpenBrowser: page pictures decoded by media.decode/1 (gfopenservice
 * docs/MEDIA_DECODE.md) on a board in this machine or a paired Cradle,
 * through gfopenservice.device. WebCore's datatypes decoder asks this first
 * (obdt_set_service_decoder); anything it cannot do goes to the datatypes
 * as before. With a decoder set, WebCore also hands SVG pictures here as
 * bitmaps instead of drawing them itself, which takes minutes on a 68k.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/io.h>
#include <proto/exec.h>

#include "devices/openservice.h"
#include "ob_media.h"

/* From WebCore's platform/image-decoders/amiga/DatatypesDecode.h. */
typedef struct {
    unsigned width;
    unsigned height;
    int hasAlpha;
    unsigned char *pixels;
} OBDTImage;
typedef int (*OBDTServiceDecoder)(const void *data, unsigned long size, OBDTImage *out);
extern void obdt_set_service_decoder(OBDTServiceDecoder);

#define MD_PROBE 1
#define MD_DECODE 2
#define MD_LARGEST 4096

static struct MsgPort *openPort;
static struct OSRequest *opened;
static UWORD handle;

static LONG call(UWORD op, const void *data, ULONG size, void *out, ULONG room, ULONG *result, ULONG *aux)
{
    struct MsgPort *port = CreateMsgPort();
    struct OSRequest *io = port ? (struct OSRequest *)CreateIORequest(port, sizeof *io) : NULL;
    LONG status = OSERR_LOST;
    if (io) {
        io->os_Req.io_Device = opened->os_Req.io_Device;
        io->os_Req.io_Unit = opened->os_Req.io_Unit;
        io->os_Req.io_Command = OSCMD_CALL;
        io->os_Service = handle;
        io->os_Op = op;
        io->os_Arg = 0;
        io->os_Flags = 2;                     /* buffer 1 is written */
        memset(io->os_Buf, 0, sizeof io->os_Buf);
        memset(io->os_Extra, 0, sizeof io->os_Extra);
        io->os_Buf[0].ob_Data = (APTR)data;
        io->os_Buf[0].ob_Length = size;
        io->os_Buf[1].ob_Data = out;
        io->os_Buf[1].ob_Length = room;
        DoIO((struct IORequest *)io);
        status = io->os_Req.io_Error ? OSERR_LOST : io->os_Status;
        *result = io->os_Result;
        *aux = io->os_Aux;
        DeleteIORequest((struct IORequest *)io);
    }
    if (port)
        DeleteMsgPort(port);
    return status;
}

static int serviceDecode(const void *data, unsigned long size, OBDTImage *out)
{
    ULONG info[6], width, height, w, h;
    unsigned char *pixels;
    if (!opened || !size)
        return 0;
    if (call(MD_PROBE, data, size, info, sizeof info, &width, &height) != OSERR_OK || info[0] != 1)
        return 0;
    w = info[4];
    h = info[5];
    if (!w || !h || w > MD_LARGEST || h > MD_LARGEST)
        return 0;
    pixels = malloc(w * h * 4);
    if (!pixels)
        return 0;
    if (call(MD_DECODE, data, size, pixels, w * h * 4, &width, &height) != OSERR_OK || width != w || height != h) {
        free(pixels);
        return 0;
    }
    out->width = w;
    out->height = h;
    out->hasAlpha = info[2] & 1;
    out->pixels = pixels;
    return 1;
}

int ob_media_open(void)
{
    static const char name[] = "media.decode/1";
    if (opened)
        return 1;
    openPort = CreateMsgPort();
    opened = openPort ? (struct OSRequest *)CreateIORequest(openPort, sizeof *opened) : NULL;
    if (!opened || OpenDevice((CONST_STRPTR)OPENSERVICE_NAME, 0, (struct IORequest *)opened, 0)) {
        if (opened)
            DeleteIORequest((struct IORequest *)opened);
        if (openPort)
            DeleteMsgPort(openPort);
        opened = NULL;
        openPort = NULL;
        return 0;
    }
    memset(opened->os_Buf, 0, sizeof opened->os_Buf);
    opened->os_Req.io_Command = OSCMD_OPEN;
    opened->os_Buf[0].ob_Data = (APTR)name;
    opened->os_Buf[0].ob_Length = sizeof name - 1;
    if (DoIO((struct IORequest *)opened) || opened->os_Status) {
        ob_media_close();
        return 0;
    }
    handle = (UWORD)opened->os_Result;
    obdt_set_service_decoder(serviceDecode);
    return 1;
}

void ob_media_close(void)
{
    if (!opened)
        return;
    obdt_set_service_decoder(NULL);
    if (handle) {
        opened->os_Req.io_Command = OSCMD_CLOSE;
        opened->os_Service = handle;
        DoIO((struct IORequest *)opened);
    }
    CloseDevice((struct IORequest *)opened);
    DeleteIORequest((struct IORequest *)opened);
    DeleteMsgPort(openPort);
    opened = NULL;
    openPort = NULL;
    handle = 0;
}
