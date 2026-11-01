/*
 * GFOpenBrowser: sessionStorage, kept in memory for as long as the page's
 * view lives (WebCore's empty provider kept nothing, so the setting was
 * off and window.sessionStorage was missing). localStorage stays off.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#pragma once

#include <WebCore/StorageNamespaceProvider.h>
#include <wtf/Ref.h>

namespace GFOpenBrowser {

Ref<WebCore::StorageNamespaceProvider> createSessionStorageProvider();

}
