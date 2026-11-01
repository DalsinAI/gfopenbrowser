/* GFOpenBrowser (DalsinAI/openamigabrowser). Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE. */
/* Symbols libstdc++ (GCC 16, amiga16.2) expects that libnix does not export. */
#include <errno.h>
#include <string.h>

int __xpg_strerror_r(int errnum, char* buf, size_t buflen)
{
    const char* msg = strerror(errnum);
    size_t len;
    if (!buf || !buflen)
        return ERANGE;
    if (!msg)
        msg = "Unknown error";
    len = strlen(msg);
    if (len >= buflen) {
        memcpy(buf, msg, buflen - 1);
        buf[buflen - 1] = 0;
        return ERANGE;
    }
    memcpy(buf, msg, len + 1);
    return 0;
}
