/*
 * GFOpenBrowser: an empty ICU common data block (OB_ICU_DATA=file).
 *
 * ICU's code refers to its linked-in data by one symbol. A program that
 * reads the data from a file (ob_icudata.c) defines that symbol here with
 * no items, so libicudata.a's 5 MB of tables is never linked; ICU then
 * finds every item in the data ob_icu_data_load hands it. The layout is
 * ICU's common data header ("CmnD", format 1) with a table of contents of
 * zero entries.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <stdint.h>
#include <unicode/udata.h>
#include <unicode/utypes.h>

struct ObIcuStubData {
    uint16_t headerSize;
    uint8_t magic1, magic2;
    UDataInfo info;
    char padding[8];
    uint32_t count, reserved;
    const void *toc[2];
};

/* icudt<major>_dat, the name U_ICUDATA_ENTRY_POINT gives (that macro is
 * ICU internal API). */
#define OB_ICU_ENTRY_POINT2(major) icudt##major##_dat
#define OB_ICU_ENTRY_POINT(major) OB_ICU_ENTRY_POINT2(major)

/* Amiga objects align to at most 8 bytes. */
const struct ObIcuStubData OB_ICU_ENTRY_POINT(U_ICU_VERSION_MAJOR_NUM) __attribute__((aligned(8))) = {
    32, 0xda, 0x27,
    {
        sizeof(UDataInfo), 0,
        U_IS_BIG_ENDIAN, U_CHARSET_FAMILY, U_SIZEOF_UCHAR, 0,
        { 0x43, 0x6d, 0x6e, 0x44 }, /* "CmnD" */
        { 1, 0, 0, 0 },
        { 0, 0, 0, 0 },
    },
    { 0 },
    0, 0,
    { 0, 0 },
};
