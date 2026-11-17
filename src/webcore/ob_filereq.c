/*
 * GFOpenBrowser: the file requester for a page's file button (see
 * ob_filereq.h). Putting the names together is plain C, tested on the host
 * (tests/test_ob_filereq.c); the requester itself is built on the Amiga only.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "ob_filereq.h"

#include <string.h>

/* How many bytes text takes as UTF-8. */
static int wide(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    int n = 0;
    for (; *p; p++)
        n += *p < 0x80 ? 1 : 2;
    return n;
}

static char *put(char *out, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    for (; *p; p++) {
        if (*p < 0x80)
            *out++ = (char)*p;
        else {
            *out++ = (char)(0xc0 | (*p >> 6));
            *out++ = (char)(0x80 | (*p & 0x3f));
        }
    }
    return out;
}

int ob_filereq_add(char *list, int size, const char *drawer, const char *file)
{
    int have, drawerLength, slash, need;
    char *out;
    if (!list || size < 1 || !file || !*file)
        return 0;
    if (!drawer)
        drawer = "";
    have = (int)strlen(list);
    drawerLength = (int)strlen(drawer);
    slash = drawerLength && drawer[drawerLength - 1] != ':' && drawer[drawerLength - 1] != '/';
    need = (have ? 1 : 0) + wide(drawer) + slash + wide(file);
    if (have + need + 1 > size)
        return 0;
    out = list + have;
    if (have)
        *out++ = '\n';
    out = put(out, drawer);
    if (slash)
        *out++ = '/';
    out = put(out, file);
    *out = 0;
    return 1;
}

#if defined(__amigaos__) || defined(AMIGA)

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/asl.h>

struct Library *AslBase;

int ob_filereq_choose(struct Window *parent, int many, char *result, int size)
{
    /* the drawer last picked from, for the next time */
    static char lastDrawer[256];
    struct FileRequester *request;
    int count = 0;
    if (!result || size < 1)
        return 0;
    result[0] = 0;
    if (!(AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 39)))
        return 0;
    request = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)parent,
        ASLFR_SleepWindow, TRUE,
        ASLFR_TitleText, (ULONG)(many ? "Choose files for the page" : "Choose a file for the page"),
        ASLFR_PositiveText, (ULONG)"Choose",
        ASLFR_InitialDrawer, (ULONG)lastDrawer,
        ASLFR_DoMultiSelect, many ? TRUE : FALSE,
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);
    if (request) {
        if (AslRequest(request, NULL)) {
            const char *drawer = request->fr_Drawer ? (const char *)request->fr_Drawer : "";
            if (many && request->fr_NumArgs > 0 && request->fr_ArgList) {
                LONG i;
                /* a file that no longer fits is left out, with those after it */
                for (i = 0; i < request->fr_NumArgs; i++) {
                    if (!ob_filereq_add(result, size, drawer, (const char *)request->fr_ArgList[i].wa_Name))
                        break;
                    count++;
                }
            } else if (request->fr_File && ob_filereq_add(result, size, drawer, (const char *)request->fr_File))
                count = 1;
            strncpy(lastDrawer, drawer, sizeof lastDrawer - 1);
            lastDrawer[sizeof lastDrawer - 1] = 0;
        }
        FreeAslRequest(request);
    }
    CloseLibrary(AslBase);
    AslBase = NULL;
    return count;
}

#endif
