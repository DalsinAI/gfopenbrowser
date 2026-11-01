/* unicode.library from a program (ul_client.c).
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#ifndef UL_CLIENT_H
#define UL_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#define UL_OPEN_OK         0
#define UL_OPEN_NO_LIBRARY 1   /* no unicode.library 1 in LIBS: */
#define UL_OPEN_NO_DATA    2   /* it cannot read ICU's data file, or is older than the program */

#define UL_DATA_FILE "icudt78b.dat"

/* Opens unicode.library and fetches its table, once; UL_OPEN_OK when ICU's
 * calls can be made. Any ICU call does this itself the first time. */
int ul_client_open(void);
/* A line for the user about a failed open. */
const char *ul_client_error(int code);

#ifdef __cplusplus
}
#endif

#endif
