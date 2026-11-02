/* GFOpenBrowser (DalsinAI/gfopenbrowser). Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE. */
/* Diagnostic abort(): print main's address and every stack word that could be
 * a return address, so the host can map them to functions with the link map. */
#include <stdint.h>
#include <stdio.h>
extern int main(int, char**);
void __real_abort(void);
void __wrap_abort(void)
{
    uintptr_t here = 0;
    volatile uintptr_t* sp = (volatile uintptr_t*)&here;
    uintptr_t m = (uintptr_t)&main;
    int i;
    printf("OS32DIAG abort main=%08lx\n", (unsigned long)m);
    for (i = 0; i < 4096; ++i) {
        uintptr_t v = sp[i];
        if (v > m - 0x2100000 && v < m + 0x2100000 && !(v & 1))
            printf("OS32DIAG s%d %08lx\n", i, (unsigned long)v);
    }
    fflush(stdout);
    __real_abort();
}
