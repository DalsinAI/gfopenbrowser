/*
 * GFOpenBrowser's ARexx commands, for its GFOPENBROWSER port (ob_browser.c
 * owns the port and the window; this file knows the commands). Plain C99,
 * no Amiga headers: tested on the host (tests/test_ob_rexx.c).
 *
 * The rules are those of every GFOpen application's port: commands in any
 * case, an argument plain or in quotes, return codes 0 done, 5 not possible
 * now, 10 an error, 20 an unknown command; on any but 0, LASTERROR gives
 * the reason. No requester is ever shown.
 *
 *   VERSION              the program's name and version
 *   OPEN <address>       load a page (OPENURL is the same; URL= may lead)
 *   RELOAD, STOP         as the buttons
 *   BACK, FORWARD        as the buttons; 5 when there is no page to go to
 *   GETURL, GETTITLE     the page's address and title
 *   STATUS               "LOADING <percent>" or "DONE 100"
 *   LOADTIME             the last finished load in milliseconds; 5 while
 *                        the first page still loads
 *   LOADCOUNT            how many loads have finished since the window
 *                        opened: a script notes it, sends OPEN, and waits
 *                        for it to go up (Rexx/SpeedTest.rexx)
 *   GETTEXT              the page's text (cut to the result's size)
 *   ACTIVATE, DEACTIVATE the window to the front and active, or to the back
 *   FAULT <code>         AmigaDOS's text for an error code
 *   LASTERROR            why the last command failed
 *   HELP                 the commands
 *   QUIT                 close the browser
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_REXX_H
#define OB_REXX_H

#include <stddef.h>

#define OB_RX_PORT "GFOPENBROWSER"
#define OB_RX_OK 0
#define OB_RX_NOW 5
#define OB_RX_ERROR 10
#define OB_RX_FAIL 20

/* What the commands do, given by the program that has the page. Text is
 * the program's own (Latin-1 on the Amiga). Any call may be NULL: its
 * command then answers 5. */
typedef struct {
    void *user;
    const char *version;                               /* VERSION's answer */
    void (*open)(void *user, const char *address);
    void (*reload)(void *user);
    void (*stop)(void *user);
    int (*back)(void *user);                           /* 0: no page to go back to */
    int (*forward)(void *user);
    const char *(*url)(void *user);
    const char *(*title)(void *user);
    int (*loading)(void *user, int *percent);          /* 1 while a page loads */
    long (*load_ms)(void *user);                       /* the last finished load, -1: none yet */
    long (*load_count)(void *user);                    /* loads finished so far */
    int (*text)(void *user, char *buffer, int size);
    void (*activate)(void *user, int front);
    int (*fault)(void *user, long code, char *buffer, int size);
    void (*quit)(void *user);
} ob_rexx_host;

typedef struct {
    ob_rexx_host host;
    char lasterror[128];
} ob_rexx;

void ob_rexx_init(ob_rexx *rx, const ob_rexx_host *host);
/* One command; its answer into result (cut to size, "" when it has none).
 * Returns the return code. */
int ob_rexx_command(ob_rexx *rx, const char *command, char *result, size_t size);

#endif
