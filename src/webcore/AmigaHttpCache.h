/*
 * GFOpenBrowser: the HTTP disk cache (ob_httpcache.c) joined to WebCore's
 * network layer (AmigaNetwork.h).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#pragma once

namespace GFOpenBrowser {

// Opens the cache (defaultDir, such as "PROGDIR:Cache", unless the
// environment says otherwise; see ob_httpcache.h) and hands it to the
// network layer. Returns whether the cache is on.
bool initializeHttpCache(const char* defaultDir);
// Writes the cache's index; after this nothing is looked up or stored.
void closeHttpCache();

} // namespace GFOpenBrowser
