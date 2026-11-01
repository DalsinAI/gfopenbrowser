# GFOpenBrowser Lite 0.4 qualification

> History. Since GFOpenBrowser Lite 0.4.1 (19 October 2026) TLS is GFOpenTLS (AmiSSL is gone), the engine is `src/weblayout/` and the build is `scripts/build-openbrowser.sh`; see `docs/RELEASE_0.4.1.md`.

Date: 7 October 2026

GFOpenBrowser Lite 0.4 adds the first practical CSS cascade while preserving the small resident-GFOpenLayout architecture introduced in 0.2 and the datatype image path introduced in 0.3.

## CSS scope

0.4 supports a deliberately small computed-style layer above GFOpenLayout.

Implemented selector forms:

- element selectors;
- class selectors;
- ID selectors;
- compound selectors such as `p.lead`;
- comma-separated selector groups;
- universal selector;
- inline `style=` declarations.

Unsupported combinators, pseudo-classes, pseudo-elements and attribute selectors are ignored rather than guessed.

The cascade applies selector specificity and source order, with inline style taking precedence.

Implemented properties include the current GFOpenLayout style surface:

- foreground colour;
- background colour;
- font size;
- font weight;
- italic style;
- underline;
- line height;
- margins;
- padding;
- text alignment;
- `display: block`, `inline` and `none`.

Colours accept hexadecimal values, `rgb(...)` and a small useful named-colour set.

## Stylesheets

The HTML Lite parser now preserves stylesheet links from:

```html
<link rel="stylesheet" href="...">
```

GFOpenBrowser resolves those URLs against the current page and fetches them through the existing HTTP/HTTPS/AmiSSL stack.

The browser currently loads at most eight page stylesheets and bounds combined external CSS to 256 KiB.

Inline `<style>` content and fetched external CSS are combined into the same small cascade.

## Architecture

```text
HTTP / HTTPS / AmiSSL
        |
HTML Lite + stylesheet discovery
        |
CSS Lite cascade
        |
gfopenlayout.library 2.0
        |
GFOpenLayout display list
        |
RastPort + datatype images
```

GFOpenLayout remains unaware of CSS, networking and browser policy.

## Qualification

Qualified on AmigaOS 3.2.3, AC090-native, 68040 + FPU.

Resident GFOpenLayout stress:

```text
LAYOUTSTRESS PASS ops=49 height=280
```

CSS unit test:

```text
CSSLITE PASS rules=5
```

A real HTTPS stylesheet was fetched inside the guest:

```text
FETCH_OK status=200
type=text/css
bytes=14863
url=https://www.w3.org/StyleSheets/Core/Traditional.css
```

Both static and resident-library HTML/CSS tests passed:

```text
HTMLLITE PASS title="GFOpenBrowser & GFOpenLayout"
wide_px=230 narrow_px=328 ops=24 script=1
```

HTTPS remained live:

```text
FETCH_OK status=200 type=text/html charset=utf-8 bytes=577
url=https://example.com/
```

Datatype qualification remained green for PNG, JPEG, GIF and WebP, including a WebP fetched across the network.

The actual GFOpenBrowser 0.4 executable was launched against the W3C First CSS example page. After the qualification delay AmigaDOS still reported:

```text
Process 4: Loaded as command: DH1:OB02/GFOpenBrowser
```

## Qualified build

- GFOpenBrowser 0.4: 93,436 bytes
- gfopenlayout.library 2.0: 11,272 bytes
- resident HTML/CSS test: 48,664 bytes
- static HTML/CSS test: 54,348 bytes
- CSS Lite test: 37,112 bytes
- obfetch: 44,484 bytes

The browser remains under 100 KiB excluding shared system libraries and datatype implementations.

## Known 0.4 limits

- no descendant/child combinators yet;
- no pseudo-classes or pseudo-elements;
- no CSS Grid/Flexbox;
- no media queries;
- no CSS background-image rendering yet;
- unsupported CSS declarations are ignored;
- tables are still semantic rather than true grid layout;
- forms are not interactive yet;
- JavaScript remains outside the Lite engine and belongs to the full browser.

These are intentional boundaries. The Lite engine continues to grow from real page requirements rather than by cloning WebKit.
