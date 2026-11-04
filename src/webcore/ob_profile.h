/*
 * Where a page's time goes, for obcore-view -profile (ob_profile.cpp):
 * script compiles and top-level runs by URL, calls into JavaScript, style
 * updates and layouts, each scope's own time.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef OB_PROFILE_H
#define OB_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Starts timing (before the page loads). */
void ob_profile_start(void);
/* Prints what was timed: OBVIEW_PROFILE with the totals (compile, run,
 * calls, style, layout: _ms and how many), then OBVIEW_SCRIPT lines, one
 * a script, the most costly first. */
void ob_profile_report(void);

#ifdef __cplusplus
}
#endif

#endif
