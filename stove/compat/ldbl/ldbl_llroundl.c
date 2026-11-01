/* GFOpenBrowser. Copyright (c) 2026 AmiCradle Team. MIT licence. */
/* AmiCradle os32-gcc16: long double via double (libnix has none). */
#include <math.h>
long long llroundl(long double x) { return llround((double)x); }
