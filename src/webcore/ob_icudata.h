/*
 * GFOpenBrowser: ICU's data read from a file at start (OB_ICU_DATA=file)
 * instead of linked into every program.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_ICUDATA_H
#define OB_ICUDATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Reads icudt<major>b.dat from LOCALE:Unicode/, then PROGDIR:Unicode/, and
 * hands it to ICU. Call before anything uses ICU (JSC::initialize does).
 * Returns 1 when ICU has its data (also on later calls), 0 when no file was
 * found or ICU refused it; a line on stderr says which. */
int ob_icu_data_load(void);

#ifdef __cplusplus
}
#endif

#endif
