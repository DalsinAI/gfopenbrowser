/* unicode.library, our own code: utext_setup and utext_close.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * The two UText calls programs make: WebKit's text providers (Latin-1 and
 * UTF-16 with context) set up their UTexts with utext_setup and give them
 * to ubrk_setUText; utext_close ends them. Fields and flags as ICU 78 lays
 * them out (UText is public; the flags are ICU's private values), so a
 * UText either side made can be closed by the other.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <unicode/utext.h>

#include "uo_own.h"

enum { HEAP_ALLOCATED = 1, EXTRA_HEAP_ALLOCATED = 2, OPEN = 4 };

/* a UText with its extra space after it, aligned as ICU's ExtendedUText */
union align { long double d; void *p; long long l; };
struct extended { UText ut; union align extension; };

UText *uo_utext_setup(UText *ut, int32_t extraSpace, UErrorCode *status)
{
    if (!status || U_FAILURE(*status))
        return ut;
    if (!ut) {
        size_t need = sizeof(UText);
        static const UText empty = UTEXT_INITIALIZER;
        if (extraSpace > 0)
            need = sizeof(struct extended) + extraSpace - sizeof(union align);
        ut = malloc(need);
        if (!ut) {
            *status = U_MEMORY_ALLOCATION_ERROR;
            return NULL;
        }
        *ut = empty;
        ut->flags |= HEAP_ALLOCATED;
        ut->extraSize = extraSpace > 0 ? extraSpace : 0;
        ut->pExtra = &((struct extended *)ut)->extension;
    } else {
        if (ut->magic != UTEXT_MAGIC) {
            *status = U_ILLEGAL_ARGUMENT_ERROR;
            return ut;
        }
        if ((ut->flags & OPEN) && ut->pFuncs && ut->pFuncs->close)
            ut->pFuncs->close(ut);
        ut->flags &= ~OPEN;
        if (extraSpace > ut->extraSize) {
            if (ut->flags & EXTRA_HEAP_ALLOCATED) {
                free(ut->pExtra);
                ut->extraSize = 0;
            }
            ut->pExtra = malloc(extraSpace);
            if (!ut->pExtra)
                *status = U_MEMORY_ALLOCATION_ERROR;
            else {
                ut->extraSize = extraSpace;
                ut->flags |= EXTRA_HEAP_ALLOCATED;
            }
        }
    }
    if (U_SUCCESS(*status)) {
        ut->flags |= OPEN;
        ut->context = NULL;
        ut->chunkContents = NULL;
        ut->p = ut->q = ut->r = NULL;
        ut->a = ut->b = ut->c = 0;
        ut->chunkOffset = ut->chunkLength = 0;
        ut->chunkNativeStart = ut->chunkNativeLimit = 0;
        ut->nativeIndexingLimit = 0;
        ut->providerProperties = 0;
        ut->privA = ut->privB = 0;
        ut->privC = 0;
        ut->privP = NULL;
        if (ut->pExtra && ut->extraSize > 0)
            memset(ut->pExtra, 0, ut->extraSize);
    }
    return ut;
}

UText *uo_utext_close(UText *ut)
{
    if (!ut || ut->magic != UTEXT_MAGIC || !(ut->flags & OPEN))
        return ut;
    if (ut->pFuncs && ut->pFuncs->close)
        ut->pFuncs->close(ut);
    ut->flags &= ~OPEN;
    if (ut->flags & EXTRA_HEAP_ALLOCATED) {
        free(ut->pExtra);
        ut->pExtra = NULL;
        ut->flags &= ~EXTRA_HEAP_ALLOCATED;
        ut->extraSize = 0;
    }
    ut->pFuncs = NULL;
    if (ut->flags & HEAP_ALLOCATED) {
        ut->magic = 0;
        free(ut);
        return NULL;
    }
    return ut;
}
