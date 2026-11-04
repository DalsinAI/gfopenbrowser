/*
 * GFOpenBrowser: the folder of translated scripts (WebKit patch 0099,
 * AmigaScriptBytecode.h): what JavaScriptCore made from a page's or a
 * worker's script, kept so the next visit loads it instead of compiling.
 * ob_scriptcache_trim() keeps the folder under its size at start: the files
 * used longest ago go first.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_SCRIPTCACHE_H
#define OB_SCRIPTCACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#define OB_SCRIPTCACHE_DIR "PROGDIR:Cache/Scripts"
#define OB_SCRIPTCACHE_MAX (32L * 1024 * 1024)

/* The folder's .jsc files down to three quarters of max bytes, when they
 * are over it: oldest first. Returns the bytes left. */
long ob_scriptcache_trim(const char *dir, long max);

#ifdef __cplusplus
}
#endif

#endif
