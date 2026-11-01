/*
 * GFOpenBrowser: WebCore's editor client (typing into form fields).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#pragma once

#include <wtf/UniqueRef.h>

namespace WebCore {
class EditorClient;
}

namespace GFOpenBrowser {

UniqueRef<WebCore::EditorClient> createEditorClient();

} // namespace GFOpenBrowser
