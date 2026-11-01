/*
 * jsimage, the image's side (jsimage.h): JavaScriptEngine runs a client
 * that its jsimage_entry handed it, in this process, after this program's
 * own start-up. Built into an archive so that the client finds these
 * names in the image.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "jsimage.h"

void *jsimage_client;
unsigned long jsimage_id = 0x4A53494EUL;   /* jsimage.py writes the real one */

/* libnix's copy of the command line the image was started with */
extern char *__commandline;
extern unsigned long __commandlen;

/* long jsimage_call(void *start, char *args, long len): the client's
 * start-up called as RunCommand would call it, every register kept */
long jsimage_call(void *start, char *args, long len);
__asm__(
    "       .text\n"
    "       .even\n"
    "_jsimage_call:\n"
    "       movem.l d2-d7/a2-a6,-(sp)\n"
    "       move.l  48(sp),a1\n"
    "       move.l  52(sp),a0\n"
    "       move.l  56(sp),d0\n"
    "       jsr     (a1)\n"
    "       movem.l (sp)+,d2-d7/a2-a6\n"
    "       rts\n");

long jsimage_run_client(void)
{
    void *start = jsimage_client;
    jsimage_client = 0;
    return jsimage_call(start, __commandline, (long)__commandlen);
}
