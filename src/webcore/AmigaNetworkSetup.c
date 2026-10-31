/*
 * OpenBrowser: WebCore's network thread on AmigaOS.
 *
 * Every load goes through openweb.fetch (WebKit patch 0075, no curl). Its
 * native transport opens its own bsdsocket.library, opentls.library and
 * datatypes.library bases on the network thread, and the card needs none,
 * so the program opens no socket base itself and starts without a TCP/IP
 * stack. Until patch 0075 this file opened bsdsocket.library on the main
 * task and on curl's thread, and until 19 October 2026 AmiSSL as well.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <exec/types.h>
#include <proto/exec.h>
#ifdef OB_OPENSERVICE
#include "ob_media.h"
#endif

#include "ob_network.h"

static BYTE mainPriority;

/* ROM math libraries opened on the way can leave the FPU in single precision. */
static void resetFPCR(void)
{
    __asm__ volatile ("fmove.l %0,%%fpcr" : : "d" (0));
}

int ob_network_open(void)
{
    mainPriority = FindTask(NULL)->tc_Node.ln_Pri;
#ifdef OB_OPENSERVICE
    /* Page pictures, SVG included, decoded by a board in this machine or a
     * paired Cradle, when one offers it. */
    ob_media_open();
#endif
    resetFPCR();
    return 1;
}

void ob_network_close(void)
{
#ifdef OB_OPENSERVICE
    ob_media_close();
#endif
}

void ob_network_thread_started(void)
{
    /* One priority above the browser: a TLS handshake is seconds of
     * public-key arithmetic on a 68k, and a server hangs up on a client that
     * takes too long, so the page's work must not slow it down. The thread
     * spends most of its time waiting on the network. */
    SetTaskPri(FindTask(NULL), mainPriority + 1);
    resetFPCR();
}
