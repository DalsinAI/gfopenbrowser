# GFOpenLayout Lite browser path

Status: first semantic adapter built, 7 October 2026. Since 0.4.1 (19
October 2026) the engine lives in `src/weblayout/` behind one header,
`weblayout.h`, the bones of weblayout.library (`docs/WEBLAYOUT_LIBRARY.md`);
`ob_html_lite` is `wl_html` there, `ob_css_lite` `wl_css`, `ob_layout_view`
`wl_paint` and `ob_image_dt` `wl_image_dt`. TLS is GFOpenTLS, not AmiSSL.

This branch adds the first engine slice for the super-lightweight GFOpenBrowser path without changing the working GadTools browser window yet.

## Current pipeline

```text
HTML bytes
   |
ob_html_lite
   |
GFOpenLayout semantic tree
   |
GFOpenLayout layout + display list
```

The adapter preserves semantic structure rather than flattening a page to the old numbered-link text view.

Currently represented:

- title;
- body/div/section/article/header/footer/main/nav;
- headings;
- paragraphs;
- links with href + activate action;
- strong/bold, emphasis/italic, underline, code and span;
- blockquotes and preformatted text;
- unordered and ordered lists;
- images with src/alt/width/height metadata;
- line breaks and horizontal-rule fallback;
- table/row/cell semantic roles.

Scripts and style blocks are detected and skipped. They are never executed by this engine.

Tables are preserved semantically but counted as unsupported until GFOpenLayout has real table-grid geometry.

## Test

Host:

```sh
OPENLAYOUT_SRC=../openamigalayout tests/run-host-tests.sh
```

AmigaOS 3.x / 68040 + FPU:

```sh
TOOLCHAIN_DIR=... NDK_INCLUDE=.../OpenNDK/Include_H OPENLAYOUT_SRC=../openamigalayout \
    TESTS=1 scripts/build-openbrowser.sh
```

(`build/os3/tests/HTMLLiteTest`; CI builds it on every change,
`.github/workflows/lite.yml`.)

The first m68k test binary is 37,216 bytes and verifies:

- entity decoding and title extraction;
- script/style detection;
- heading/paragraph/list/image display-list output;
- real link semantics and href;
- nearest-actionable hit testing;
- stable link identity after reflow from 360 CSS px to 180 CSS px.

## Live viewport milestone — 7 October 2026

The working GFOpenBrowser Lite shell now has a real GFOpenLayout viewport instead of the old numbered-text page listview.

The integration:

- measures text against the active Amiga RastPort/font;
- lays the semantic document to the actual page width;
- draws text, backgrounds, rules and image placeholders directly into the window RastPort;
- uses a GadTools vertical scroller over the GFOpenLayout document height;
- resolves mouse coordinates through semantic actionable hit-testing;
- resolves a clicked link relative to the current page and feeds it into the existing navigation/history path;
- keeps the existing network layer, history, Links... chooser and ARexx port.

The first real OS 3.2.3 / AC090 68040 qualification fetched today's 577-byte `http://example.com/`, laid it out and painted it in the GFOpenBrowser window on GFOpenRTG. The resulting browser binary is 80,900 bytes and does not contain WebCore or JavaScriptCore.

The semantic HTML/link test remains 37,216 bytes on m68k and verifies stable link identity through reflow plus actionable hit resolution.

## Next integration slice

1. decode and paint image nodes through Amiga datatypes rather than placeholders;
2. add better font/style selection while keeping the layout measurement callback authoritative;
3. move fill/mask/glyph raster work to `pixman.library`;
4. add local/headless golden-page pixel tests;
5. qualify click-through navigation on a linked real page in the guest;
6. add the explicit `Open in Full Browser` escalation for script-heavy/unsupported pages.

The full WebKit engine remains available for pages that require JavaScript or unsupported modern layout. The lightweight path does not grow into a second WebKit.

## 0.2 release gate

GFOpenBrowser 0.2 is the first resident-GFOpenLayout build. Its gate is:

- `gfopenlayout.library` 2.0 opens from `LIBS:` on OS 3.2.3 / AC090 68040;
- the semantic HTML adapter passes through the resident ABI;
- the GadTools browser renders through the GFOpenLayout display list;
- scrolling and semantic link activation remain live;
- the existing HTTP/HTTPS (AmiSSL then; GFOpenTLS since 0.4.1), redirects, URL and charset plumbing remains linked;
- a guest launch against an HTTPS page completes without falling back to the full WebKit engine.

The full engine remains a separate escalation path, not a dependency of the Lite renderer.
