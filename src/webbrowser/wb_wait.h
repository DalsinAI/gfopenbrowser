/* webbrowser.library's wait for its engine, one look a second, in plain C so
 * the host tests read it (tests/test_wb_engine_wait.c).
 *
 * The engine is started by a Shell of its own, a process called
 * WebBrowserEngine, and is ready when its port is there. When that process
 * has been seen and is gone with no port made, the engine's program never
 * ran: it was refused (Tata's "Don't run it"), or it failed at once. Waiting
 * the whole time allowed for it would only look like a browser that hangs
 * (17 November 2026, on a Pi 5).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef WB_WAIT_H
#define WB_WAIT_H

#define WB_WAIT_ON      0   /* look again in a second */
#define WB_WAIT_READY   1   /* the engine's port is there */
#define WB_WAIT_ENDED (-1)  /* its process was there and has gone, with no port */
#define WB_WAIT_LATE  (-2)  /* the time allowed is over */

/* port, process: whether each is there now. *seen: whether the process has
 * ever been there (0 before the first look). second: seconds waited so far. */
static inline int wb_engine_wait(int port, int process, int *seen, int second, int limit)
{
    if (port)
        return WB_WAIT_READY;
    if (process)
        *seen = 1;
    else if (*seen)
        return WB_WAIT_ENDED;   /* never seen: a system that doesn't name the Shell so; only the time ends the wait */
    return second >= limit ? WB_WAIT_LATE : WB_WAIT_ON;
}

#endif
