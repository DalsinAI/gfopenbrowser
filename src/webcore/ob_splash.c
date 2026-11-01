/*
 * The browser's side of the title window (ob_splash.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include "ob_splash.h"

void ob_splash(const char *text, int percent)
{
    struct OBSplashMessage *message = AllocVec(sizeof *message, MEMF_PUBLIC | MEMF_CLEAR);
    struct MsgPort *port;
    if (!message)
        return;
    message->message.mn_Length = sizeof *message;
    message->percent = percent;
    if (text)
        strncpy(message->text, text, sizeof message->text - 1);
    Forbid();
    port = FindPort((CONST_STRPTR)OB_SPLASH_PORT);
    if (port)
        PutMsg(port, &message->message);
    Permit();
    if (!port)
        FreeVec(message);
}
