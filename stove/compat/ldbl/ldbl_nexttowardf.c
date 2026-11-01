/* GFOpenBrowser. Copyright (c) 2026 AmigaChrome Team. MIT licence. */
/* AmiCradle os32-gcc16: long double via double (libnix has none). */
#include <math.h>
float nexttowardf(float x, long double y) { return nextafterf(x, (float)y); }
