/*
 * javascript.library engine: translate once. A script evaluated with a
 * cache folder keeps what JavaScriptCore made from it in a file there: the
 * script's own code when it is first run, and each function's when that
 * function is first called. The next run of the same text loads those and
 * makes nothing again. The file is written when the script task ends.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef JSL_CACHE_H
#define JSL_CACHE_H

#include <JavaScriptCore/JavaScript.h>

struct JSLCacheList;

/* JSEvaluateScript, with the translations kept in cacheDir (a folder; NULL
 * or "" keeps none). The source is remembered in *list until
 * jsl_cache_commit. */
JSValueRef jsl_cache_evaluate(JSContextRef ctx, JSStringRef source, JSStringRef url,
                              const char *cacheDir, JSLCacheList **list, JSValueRef *exception);

/* Writes every new translation in *list to its file, and empties it. On the
 * script thread, while the context is whole. */
void jsl_cache_commit(JSLCacheList **list);

#endif
