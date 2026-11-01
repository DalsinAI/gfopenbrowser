/*
 * GFOpenBrowser: ICU's data read from a file at start (ob_icudata.h).
 *
 * With OB_ICU_DATA=file the programs link ob_icustub.c instead of the
 * 5 MB of tables in libicudata.a, and this reads the same tables, ICU's
 * common data file, into memory once and gives them to ICU with
 * udata_setCommonData. The engine file gets that much smaller; a later
 * unicode.library can share one copy between programs.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "ob_icudata.h"

#include <stdio.h>

#if OB_ICU_LIBRARY
/* OB_ICU_DATA=library: ICU, code and data, is unicode.library's. Opening
 * it here, at start, gives a clear message when it or its data is missing;
 * otherwise the first ICU call would open it. */
#include <ul_client.h>

int ob_icu_data_load(void)
{
    int code = ul_client_open();

    if (code == UL_OPEN_OK)
        return 1;
    fprintf(stderr, "GFOpenBrowser: %s\n", ul_client_error(code));
    return 0;
}

#else

#include <stdlib.h>
#include <unicode/udata.h>
#include <unicode/utypes.h>

#define OB_ICU_DATA_FILE_NAME U_ICUDATA_NAME ".dat"

static const char *const dataFolders[] = {
    "LOCALE:Unicode/",
    "PROGDIR:Unicode/",
};

static int loaded;

/* The whole file in one block, 16-byte aligned as ICU's tables want.
 * NULL when it cannot be read. */
static void *readDataFile(const char *path, long *sizeOut)
{
    FILE *f = fopen(path, "rb");
    void *data = NULL;
    long size;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
        data = aligned_alloc(16, ((size_t)size + 15) & ~(size_t)15);
        if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
            free(data);
            data = NULL;
        }
        *sizeOut = size;
    }
    fclose(f);
    return data;
}

int ob_icu_data_load(void)
{
    char path[64];
    unsigned i;

    if (loaded)
        return 1;
    for (i = 0; i < sizeof dataFolders / sizeof dataFolders[0]; i++) {
        long size = 0;
        void *data;
        UErrorCode error = U_ZERO_ERROR;

        snprintf(path, sizeof path, "%s%s", dataFolders[i], OB_ICU_DATA_FILE_NAME);
        if (!(data = readDataFile(path, &size)))
            continue;
        udata_setCommonData(data, &error);
        if (U_FAILURE(error)) {
            fprintf(stderr, "GFOpenBrowser: %s is not ICU " U_ICU_VERSION " data (%s)\n", path, u_errorName(error));
            free(data);
            continue;
        }
        /* ICU keeps the block for the rest of the run. */
        loaded = 1;
        return 1;
    }
    fprintf(stderr, "GFOpenBrowser: no Unicode data, put " OB_ICU_DATA_FILE_NAME " in LOCALE:Unicode/ or PROGDIR:Unicode/\n");
    return 0;
}

#endif
