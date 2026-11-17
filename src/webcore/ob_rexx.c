/*
 * GFOpenBrowser's ARexx commands (see ob_rexx.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "ob_rexx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char helpText[] =
    "VERSION OPEN OPENURL RELOAD STOP BACK FORWARD GETURL GETTITLE STATUS LOADTIME GETTEXT"
    " ACTIVATE DEACTIVATE FAULT LASTERROR HELP QUIT";

static int upper(int c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

static int blank(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Whether command starts with word (any case) as a whole word; *rest is
 * then what follows it, without leading blanks. */
static int word(const char *command, const char *name, const char **rest)
{
    size_t i;
    for (i = 0; name[i]; i++)
        if (upper((unsigned char)command[i]) != name[i])
            return 0;
    if (command[i] && !blank((unsigned char)command[i]) && command[i] != '=')
        return 0;
    while (blank((unsigned char)command[i]))
        i++;
    *rest = command + i;
    return 1;
}

static void put(char *result, size_t size, const char *text)
{
    if (size)
        snprintf(result, size, "%s", text ? text : "");
}

static int fail(ob_rexx *rx, int rc, const char *why)
{
    snprintf(rx->lasterror, sizeof rx->lasterror, "%s", why);
    return rc;
}

/* An argument as ReadArgs would give it: an optional KEYWORD= or KEYWORD
 * before it, quotes round it taken off, blanks at its end dropped. */
static int argument(const char *arg, const char *keyword, char *out, size_t size)
{
    const char *rest;
    size_t n;
    if (!size)
        return 0;
    if (keyword && word(arg, keyword, &rest) && (*rest || arg[strlen(keyword)] == '=')) {
        arg = rest;
        if (*arg == '=')
            arg++;
        while (blank((unsigned char)*arg))
            arg++;
    }
    n = strlen(arg);
    while (n && blank((unsigned char)arg[n - 1]))
        n--;
    if (n >= 2 && arg[0] == '"' && arg[n - 1] == '"') {
        arg++;
        n -= 2;
    }
    if (n >= size)
        return 0;
    memcpy(out, arg, n);
    out[n] = 0;
    return n > 0;
}

void ob_rexx_init(ob_rexx *rx, const ob_rexx_host *host)
{
    memset(rx, 0, sizeof *rx);
    if (host)
        rx->host = *host;
}

static int run(ob_rexx *rx, const char *command, char *result, size_t size)
{
    const ob_rexx_host *h = &rx->host;
    const char *arg;
    while (blank((unsigned char)*command))
        command++;
    if (word(command, "VERSION", &arg)) {
        put(result, size, h->version ? h->version : "GFOpenBrowser");
        return OB_RX_OK;
    }
    if (word(command, "OPENURL", &arg) || word(command, "OPEN", &arg)) {
        char address[2048];
        if (!argument(arg, "URL", address, sizeof address))
            return fail(rx, OB_RX_ERROR, *arg ? "OPEN: the address is too long" : "OPEN needs an address");
        if (!h->open)
            return fail(rx, OB_RX_NOW, "OPEN: no page to load it in");
        h->open(h->user, address);
        return OB_RX_OK;
    }
    if (word(command, "RELOAD", &arg)) {
        if (!h->reload)
            return fail(rx, OB_RX_NOW, "RELOAD: no page");
        h->reload(h->user);
        return OB_RX_OK;
    }
    if (word(command, "STOP", &arg)) {
        if (!h->stop)
            return fail(rx, OB_RX_NOW, "STOP: no page");
        h->stop(h->user);
        return OB_RX_OK;
    }
    if (word(command, "BACK", &arg))
        return h->back && h->back(h->user) ? OB_RX_OK : fail(rx, OB_RX_NOW, "BACK: no page to go back to");
    if (word(command, "FORWARD", &arg))
        return h->forward && h->forward(h->user) ? OB_RX_OK : fail(rx, OB_RX_NOW, "FORWARD: no page to go forward to");
    if (word(command, "GETURL", &arg)) {
        if (!h->url)
            return fail(rx, OB_RX_NOW, "GETURL: no page");
        put(result, size, h->url(h->user));
        return OB_RX_OK;
    }
    if (word(command, "GETTITLE", &arg)) {
        if (!h->title)
            return fail(rx, OB_RX_NOW, "GETTITLE: no page");
        put(result, size, h->title(h->user));
        return OB_RX_OK;
    }
    if (word(command, "STATUS", &arg)) {
        int percent = 0;
        if (!h->loading)
            return fail(rx, OB_RX_NOW, "STATUS: no page");
        if (h->loading(h->user, &percent)) {
            if (percent < 0)
                percent = 0;
            if (percent > 99)
                percent = 99;
            if (size)
                snprintf(result, size, "LOADING %d", percent);
        } else
            put(result, size, "DONE 100");
        return OB_RX_OK;
    }
    if (word(command, "LOADTIME", &arg)) {
        long ms = h->load_ms ? h->load_ms(h->user) : -1;
        if (ms < 0)
            return fail(rx, OB_RX_NOW, "LOADTIME: no page has finished loading yet");
        if (size)
            snprintf(result, size, "%ld", ms);
        return OB_RX_OK;
    }
    if (word(command, "GETTEXT", &arg)) {
        if (!h->text || !size)
            return fail(rx, OB_RX_NOW, "GETTEXT: no page");
        result[0] = 0;
        h->text(h->user, result, (int)size);
        result[size - 1] = 0;
        return OB_RX_OK;
    }
    if (word(command, "ACTIVATE", &arg) || word(command, "DEACTIVATE", &arg)) {
        if (!h->activate)
            return fail(rx, OB_RX_NOW, "no window");
        h->activate(h->user, upper((unsigned char)*command) == 'A');
        return OB_RX_OK;
    }
    if (word(command, "FAULT", &arg)) {
        if (!*arg)
            return fail(rx, OB_RX_ERROR, "FAULT needs a code");
        if (!h->fault || !h->fault(h->user, atol(arg), result, (int)size))
            return fail(rx, OB_RX_NOW, "FAULT: no text for that code");
        return OB_RX_OK;
    }
    if (word(command, "LASTERROR", &arg)) {
        put(result, size, rx->lasterror);
        return OB_RX_OK;
    }
    if (word(command, "HELP", &arg)) {
        put(result, size, helpText);
        return OB_RX_OK;
    }
    if (word(command, "QUIT", &arg)) {
        if (!h->quit)
            return fail(rx, OB_RX_NOW, "QUIT: not now");
        h->quit(h->user);
        return OB_RX_OK;
    }
    return fail(rx, OB_RX_FAIL, "unknown command (HELP lists them)");
}

int ob_rexx_command(ob_rexx *rx, const char *command, char *result, size_t size)
{
    const char *arg;
    int rc;
    if (size)
        result[0] = 0;
    if (!command)
        return fail(rx, OB_RX_FAIL, "no command");
    rc = run(rx, command, result, size);
    /* A command that worked clears the reason of the one before it. */
    while (blank((unsigned char)*command))
        command++;
    if (rc == OB_RX_OK && !word(command, "LASTERROR", &arg))
        rx->lasterror[0] = 0;
    if (rc != OB_RX_OK && size)
        result[0] = 0;
    return rc;
}
