# GFOpenBrowser Lite 0.2 qualification

> History. Since GFOpenBrowser Lite 0.4.1 (19 October 2026) TLS is GFOpenTLS (AmiSSL is gone), the engine is `src/weblayout/` and the build is `scripts/build-openbrowser.sh`; see `docs/RELEASE_0.4.1.md`.

Date: 7 October 2026

GFOpenBrowser Lite 0.2 is the first build whose page renderer uses the resident `gfopenlayout.library` rather than linking GFOpenLayout into the browser.

## Architecture gate

- `gfopenlayout.library` ABI 2.0 from `DalsinAI/gfopenlayout`;
- HTML Lite builds semantic nodes through the resident ABI;
- the GadTools viewport consumes the GFOpenLayout display list;
- text is measured against the active RastPort font;
- semantic links are activated through GFOpenLayout hit testing;
- the existing GFOpenBrowser HTTP/HTTPS, redirects, URL resolution, charset and AmiSSL stack remains intact.

## Guest qualification

Qualified on AmigaOS 3.2.3 under AmiCradle AC090-native, 68040 + FPU, LAN enabled.

The static and resident HTML/GFOpenLayout tests both passed the same paragraph, link, list, image, reflow and semantic-hit cases.

The network-only `obfetch` then fetched `https://example.com/` successfully:

```text
-- Connecting
-- resolving the name
-- connecting
-- TLS handshake
-- Sending the request
-- Receiving
FETCH_OK status=200 type=text/html charset=utf-8 bytes=577 url=https://example.com/
```

After those gates, the real `GFOpenBrowser 0.2` executable was launched with the same HTTPS URL and remained present as an AmigaDOS task after the qualification delay.

## Qualified build sizes

- GFOpenBrowser: 73,816 bytes
- gfopenlayout.library 2.0: 11,272 bytes
- resident HTML Lite test: 31,276 bytes
- `obfetch`: approximately 44 KiB

The HTML parser was also changed to allocate its 64-frame parser state on the heap. This removes a small-Amiga-stack failure found during guest qualification and allows the parser to be called safely from ordinary Shell utilities, not only the browser's 64 KiB worker stack.
