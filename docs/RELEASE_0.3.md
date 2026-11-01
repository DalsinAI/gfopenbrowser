# GFOpenBrowser Lite 0.3 qualification

> History. Since GFOpenBrowser Lite 0.4.1 (19 October 2026) TLS is GFOpenTLS (AmiSSL is gone), the engine is `src/weblayout/` and the build is `scripts/build-openbrowser.sh`; see `docs/RELEASE_0.4.1.md`.

Date: 7 October 2026

GFOpenBrowser Lite 0.3 adds datatype-backed page images without adding image decoders to the browser or to GFOpenLayout.

## Architecture

The image path is:

```text
<img src>
   |
GFOpenBrowser URL resolution + HTTP/HTTPS
   |
temporary page-image file
   |
Amiga datatypes.library
   |
picture.datatype subclass
   |
screen-remapped bitmap
   |
GFOpenLayout image display op
   |
GadTools/RastPort viewport
```

GFOpenLayout continues to know only image identity and geometry. Network and decoding remain browser responsibilities.

The viewport has an image-provider callback. If no suitable datatype is installed or an image fails to load, the previous bordered alt-text placeholder remains visible rather than making the page fail.

## Datatype policy

GFOpenBrowser consumes the user's datatype ecosystem.

The standard AmigaOS 3.2.3 image provides PNG, JPEG and GIF datatypes. GFOpenImage adds WebP and other formats. GFOpenBrowser does not carry private PNG/JPEG/GIF/WebP decoders.

The picture object is kept alive while the page is displayed so remapped pens remain valid. Where supported, PDTM_SCALE is requested before layout; a datatype that does not implement scaling remains a valid image source.

## Guest qualification

Qualified on AmigaOS 3.2.3, AC090-native, 68040 + FPU, LAN enabled.

Local datatype probes:

```text
PNG   PASS source=800x600 bitmap=800x600
JPEG  PASS source=64x48 bitmap=64x48
GIF   PASS source=64x48 bitmap=64x48
WebP  PASS source=64x48 bitmap=64x48
```

Network-to-datatype integration used the same GFOpenBrowser HTTP/AmiSSL stack and the installed GFOpenImage WebP datatype:

```text
IMGNET FETCH status=200 type=image/webp bytes=9532
IMGNET PASS source=480x269 bitmap=480x269 mask=no
```

The final release chain also reran the static HTML test, resident gfopenlayout.library HTML test and HTTPS test successfully.

The actual GFOpenBrowser 0.3 executable was then launched against `https://www.wikipedia.org/`. After the qualification delay AmigaDOS still reported:

```text
Process 4: Loaded as command: DH1:OB02/GFOpenBrowser
```

## Qualified build

- GFOpenBrowser 0.3: 77,468 bytes
- gfopenlayout.library 2.0: 11,272 bytes
- obfetch: 44,484 bytes
- datatype image probe: about 15 KiB
- network + datatype image probe: about 47 KiB

## Known 0.3 limits

- image loading is currently synchronous while a page is being installed;
- page image cache is bounded at 32 images;
- data: image URLs are not decoded yet;
- missing datatypes intentionally fall back to alt text;
- alpha uses the datatype mask/bitmap facilities supplied by the installed picture class;
- full CSS image sizing and responsive images come later.

These are browser-layer limits, not GFOpenLayout ABI changes.
