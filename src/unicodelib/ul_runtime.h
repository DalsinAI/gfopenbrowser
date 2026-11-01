/* unicode.library: what ICU needs from a C library, made safe to share
 * between programs (ul_runtime.c).
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#ifndef UL_RUNTIME_H
#define UL_RUNTIME_H

/* At the library's init (memory pool, global constructors) and expunge. */
int ul_runtime_init(void);
void ul_runtime_end(void);

#endif
