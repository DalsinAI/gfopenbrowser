/* GFOpenBrowser. Copyright (c) 2026 AmigaChrome Team. MIT licence. */
/* AmiCradle os32-gcc16: long double via double (libnix has none). */
#include <math.h>
long double sinl(long double x) { return sin((double)x); }
