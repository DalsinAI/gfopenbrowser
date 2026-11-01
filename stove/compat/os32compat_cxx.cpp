/* GFOpenBrowser (DalsinAI/openamigabrowser). Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE. */
// C library calls that newlib's headers declare without extern "C" for C++
// (only under _POSIX_TIMERS), so C++ callers want mangled names libnix lacks.
#include <time.h>

extern "C" int os32_libnix_clock_gettime(clockid_t, struct timespec*) __asm__("_clock_gettime");

int clock_gettime(clockid_t clock, struct timespec* ts)
{
    return os32_libnix_clock_gettime(clock, ts);
}
