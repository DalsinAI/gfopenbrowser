/* GFOpenBrowser. Copyright (c) 2026 AmigaChrome Team. MIT licence. */
/* AmiCradle os32-gcc16: long double via double (libnix has none). */
#include <math.h>
long double fmal(long double x, long double y, long double z) { return fma((double)x, (double)y, (double)z); }
