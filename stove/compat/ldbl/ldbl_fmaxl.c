/* GFOpenBrowser. Copyright (c) 2026 AmigaChrome Team. MIT licence. */
/* AmiCradle os32-gcc16: long double via double (libnix has none). */
#include <math.h>
long double fmaxl(long double x, long double y) { return fmax((double)x, (double)y); }
