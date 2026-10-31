/*
 * Library bases that the static libraries call through. SocketBase was
 * curl's; since WebKit patch 0075 nothing in the programs calls through it
 * (openweb.fetch's native transport keeps its own bsdsocket.library base),
 * and it stays defined and empty so that no library's own copy comes in.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <exec/libraries.h>

struct Library *SocketBase;

#include <dos/dosextens.h>
#include <proto/exec.h>

/* No "Please insert volume" requesters for this program: a page, a font
 * setting or a library asking for a name that is not mounted gets an error
 * at once instead of a window waiting for someone to answer it. */
void ob_quiet_requesters(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    if (me->pr_Task.tc_Node.ln_Type == NT_PROCESS)
        me->pr_WindowPtr = (APTR)-1;
}
