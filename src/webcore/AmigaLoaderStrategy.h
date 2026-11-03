/*
 * GFOpenBrowser: WebCore's loader strategy with the network (see
 * AmigaLoaderStrategy.cpp, LGPL 2 or later).
 */
#pragma once

namespace WebCore {
class Document;
class LoaderStrategy;
}

namespace GFOpenBrowser {

WebCore::LoaderStrategy* networkLoaderStrategy();

// Tata's defer rules (ob_tata.h): the loads held for a page (by its top
// document) go now, and the page's later ones go at once.
void releaseDeferredLoads(WebCore::Document& topDocument);
// How many loads were held, and how many of them released, since the start.
void deferredLoadCounts(unsigned& held, unsigned& released);

} // namespace GFOpenBrowser
