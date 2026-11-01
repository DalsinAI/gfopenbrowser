/*
 * GFOpenBrowser: WebCore's loader strategy with the network (see
 * AmigaLoaderStrategy.cpp, LGPL 2 or later).
 */
#pragma once

namespace WebCore {
class LoaderStrategy;
}

namespace GFOpenBrowser {

WebCore::LoaderStrategy* networkLoaderStrategy();

} // namespace GFOpenBrowser
