/* GFOpenBrowser. Copyright (c) 2026 AmiCradle Team. MIT licence. */
/* AmiCradle os32-gcc16: long double via double (libnix has none). */
#include <math.h>
long double remquol(long double x, long double y, int* q) { return remquo((double)x, (double)y, q); }
