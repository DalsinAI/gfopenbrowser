# weblayout.library

Status: approved, in progress. Proposed 19 October 2026; Dale approved the
design on 20 October 2026 (01:32), with the proposed answers to the open
questions below. weblayout.library 1.0 is built (`src/weblayout/library`,
`scripts/build-weblayout-library.sh`) and its Amiga test program with it;
neither has run on an Amiga yet. GFOpenBrowser Lite keeps the engine linked
in by default (`WEBLAYOUT=linked`) until the library has run on an instance;
`WEBLAYOUT=library` builds it on the library instead.

## Why

Dale, 19 October 2026: "lite should almost create the bones of a
weblayout.library". A web page laid out and drawn into a RastPort is useful
to more than one program: GFOpenMail's HTML mail, GFOpenView's help pages,
AmigaGuide-like readers, installers' release notes, GFOpenUp's own "what's
new". Each of them today either flattens HTML to text or ships its own
renderer. One small resident engine, with no JavaScript and no network of
its own, is the Amiga way to share that.

## The layers

```text
the program        fetching (HTTP, files), history, windows, gadgets, menus
---------------------------------------------------------------- weblayout.h
weblayout          WLDocument: HTML Lite + CSS Lite -> semantic tree,
                   layout at a width, painting into a RastPort area,
                   pictures through picture.datatype, link hit-testing,
                   address resolution
GFOpenLayout         geometry, display list, hit-testing (gfopenlayout;
                   built in, or the resident gfopenlayout.library 2.0)
the OS             graphics.library, layers.library, datatypes.library
```

| Source (src/weblayout) | What it does |
| --- | --- |
| `weblayout.h` | the public calls; the only header a program includes |
| `wl_document.c` | the calls, over the parts below |
| `wl_html.c` | HTML Lite: tokeniser, entities, the semantic tree (headings, paragraphs, lists, links, images, pre, tables as roles) |
| `wl_css.c` | CSS Lite: element, class, ID and compound selectors, specificity and order, inline `style=`; colours, sizes, margins, padding, alignment, display |
| `wl_paint.c` | the display list painted into a RastPort, clipped to the page's area; text measured with the RastPort's font |
| `wl_image_dt.c` | page pictures through picture.datatype, remapped and scaled |
| `wl_url.c` | relative addresses made absolute (plain C) |
| `wl_openlayout.c/.h` | GFOpenLayout built in, or through gfopenlayout.library |
| `wl_mem.c/.h` | the one allocator: malloc linked in, an Exec pool per document in the library |
| `wl_libc.c/.h` | weblayout's own ctype, strcasecmp, strtol and snprintf |
| `wl_text.c/.h` | UTF-8 to ISO-8859-1 for the Amiga's fonts |
| `library/` | `weblayout_lib.sfd`, the resident library (`wl_library.c`), its string calls (`wl_runtime.c`) and the SDK files made from the SFD (`sdk/`) |

What stays in the program: the network (GFOpenBrowser Lite's `ob_http.c` on
GFOpenMail's bsdsocket/GFOpenTLS layer), charsets to UTF-8, temporary picture
files, history, the window and its gadgets, the scroller, the ARexx port and
the Links... chooser.

## The calls

All of them are in `weblayout.h` today with these names and arguments; as a
library they keep them and gain a base.

| Call | What |
| --- | --- |
| `WL_NewDocument(data, length, kind, base_url)` | a page from UTF-8 bytes (`WLKIND_HTML` or `WLKIND_TEXT`), its own address for relative ones; the bytes are copied |
| `WL_DisposeDocument(doc)` | everything the document holds, pictures too |
| `WL_GetTitle(doc)` | `<title>`, UTF-8 |
| `WL_CountResources(doc, kind)` / `WL_GetResource(doc, kind, i, &res)` | what the page asks for: `WLRES_STYLESHEET` (after creation) and `WLRES_IMAGE` (after layout, with its box), as absolute addresses |
| `WL_AddStyleSheet(doc, css, length)` | CSS the program fetched; the next layout builds the page again with it (256 KiB at most in all) |
| `WL_Layout(doc, &view)` | lay out at `wv_Width`, measuring with `wv_Font` in `wv_RPort` |
| `WL_ContentHeight(doc)` | for the scroller |
| `WL_SetImageFile(doc, id, path, screen)` | decode a picture the program fetched to a file, for the screen |
| `WL_Draw(doc, &view)` | paint into the view's area at `wv_ScrollY`; a NULL doc clears it |
| `WL_HitLink(doc, &view, x, y)` | the absolute address of the link under the pointer |
| `WL_ResolveURL(base, href, out, len)` | the resolver on its own |
| `WL_Open()` / `WL_Close()` | today: the resident GFOpenLayout and datatypes; as a library these go into its Open and Expunge |

`struct WLView` (RastPort, font, the area, scroll, four pens) and
`struct WLResource` carry their own size (`wv_Size`, `wr_Size`), so later
versions can add fields without breaking older programs.

## Ownership

- A `WLDocument` belongs to the task that made it and owns everything in it:
  the copied bytes, the CSS added, the GFOpenLayout tree, the decoded
  pictures. `WL_DisposeDocument` frees all of it.
- Strings the library hands out (`WL_GetTitle`, `wr_URL`, `WL_HitLink`) are
  the document's: a picture's `wr_URL` and `WL_HitLink`'s answer are good
  until the next such call on the same document; a stylesheet's and the
  title until disposal. Copy them to keep them.
- The program keeps a picture's file until it disposes of the document
  (picture.datatype may read it lazily).
- The RastPort, font and screen are the program's; the library only draws
  through them between the call's start and end.

## Threading

- One document is used by one task at a time; the library takes no locks
  on a document. Two tasks with two documents need nothing.
- As a library, its own global state is only the bases it opens
  (graphics, layers, datatypes, openlayout), opened in its LibOpen.
- No call waits on the network or a signal; the slow calls are
  `WL_Layout` (text measuring) and `WL_SetImageFile` (decoding). A program
  that wants to stay responsive runs those on its own worker process.

## Before it can be a library: done

Each step below is done; what was done follows it.

1. Memory: the engine uses the C library's malloc and free (libnix). A
   resident library cannot rely on a caller's C library: it needs an
   Exec-pool runtime, as gfopenlayout.library's `openlayout_runtime.c`
   already has. The work: route `wl_*.c`'s allocations through one
   allocator (`wl_alloc`/`wl_free`) and give the library build a pool per
   document.
   Done: `wl_mem.c`. Every block comes from `wl_alloc(pool, n)` (cleared)
   or `wl_realloc`, and goes back through `wl_free(p)`. Linked in, that
   is malloc and free. In the library (`WL_EXEC_POOLS`), each document
   gets an Exec pool (CreatePool, 8 KB puddles) when it is made, and its
   pool goes when it is disposed of, with anything still in it.
2. libc in the engine: `snprintf`, `strtol`, `ctype` and `strcasecmp`
   need the same treatment (small own versions, or `utility.library`).
   Done: `wl_libc.c` has weblayout's own versions: ctype (ASCII),
   `wl_strcasecmp`, `wl_strncasecmp`, `wl_strtol`, `wl_atoi` and
   `wl_snprintf` (`%s`, `%.*s`, `%c`, `%d`, `%u`, `%x`, `%ld` and the
   rest of what the engine writes). CSS `rgb()` no longer uses sscanf.
   Host tests compare them with the C library's (`tests/test_wl_libc.c`).
   The library adds the memory and string calls the compiler and the engine
   use (`library/wl_runtime.c`) and links with `-nostdlib`. Of the C
   toolchain it takes only libgcc's 64-bit division.
3. `oam_utf8_to_latin1` (GFOpenMail's `oam_text.c`) comes into
   `wl_paint.c` as its own small converter.
   Done: `wl_text.c`, `wl_utf8_to_latin1(pool, utf8, length)`. It
   converts a slice of a text node in place, so the painter no longer
   copies the slice first. weblayout needs nothing of GFOpenMail now.
4. The SFD, the LVO table, `proto/`, `inline/` and `clib/` headers, made
   the way gfopenlayout's `library/openlayout_lib.sfd` and GFOpenSDK make
   them; then a GFOpenNDK entry and a GFOpenUp part (`LIBS:weblayout.library`,
   gfopenlayout.library beside it).
   Done: `src/weblayout/library/weblayout_lib.sfd` (bias 30, the calls in
   `weblayout.h`'s order, `WL_NewDocument` at -30 and `WL_Close` at
   -108). `scripts/gen-weblayout-sdk.sh` runs GFOpenSDK's
   `OpenNDK/tools/ndkgen.py` on it, which writes
   `library/sdk/{FD,Include_H/{clib,inline,pragmas,proto},Include_I/lvo}`.
   CI fails when they are stale. The resident library is
   `library/wl_library.c`: a romtag; Open, Close and Expunge; one vector
   per call in the SFD's registers. Its first Open opens graphics.library
   39, layers.library 39, gfopenlayout.library 2 and, when there is one,
   datatypes.library 44 (without it pictures draw as their boxes). Expunge
   closes them. Not done yet: the GFOpenNDK entry and the GFOpenUp part.
5. Tests: the Amiga test programs open the library from `LIBS:` (as
   gfopenlayout's `LayoutLibTest` does), and the host tests stay.
   Done: `tests/test_wl_library.c`, built as `WebLayoutLibTest`. It opens
   `LIBS:weblayout.library` 1, then on a small page in an off-screen
   RastPort with topaz 8 it checks the title, the stylesheet and picture
   resources, layout, drawing, the link under a point, the resolver and
   plain text. It then makes and disposes of 50 pages and checks that the
   free memory comes back. The host tests stay, plus `test_wl_libc`.

None of that changes `weblayout.h`'s calls, which is the point of fixing
them now.

## The questions, answered

Approved with the proposed answers (20 October 2026):

- The name: weblayout.library, version 1 (`WEBLAYOUT_NAME`,
  `WEBLAYOUT_VERSION`), not a version of gfopenlayout.library with the web
  parts in it. It uses gfopenlayout.library 2.
- Where the source lives: here in `src/weblayout` while GFOpenBrowser Lite is
  its only user. It moves to a repository of its own when a second program
  uses it.
- Pictures from memory (`WL_SetImageData`): not in version 1. Pictures come
  from files only. A later version can add the call at the end of the SFD
  once picture.datatype's memory source (DTST_MEMORY, datatypes 44+) is
  known to work for every format.
- Fonts: one font for the page in version 1 (bold, italic and underline as
  soft styles). A font list will come later, in a larger `struct WLView`
  marked by a new `wv_Size`.

## Building it

```sh
TOOLCHAIN_DIR=... NDK_INCLUDE=.../OpenNDK/Include_H OPENLAYOUT_SRC=.../gfopenlayout \
    scripts/build-weblayout-library.sh            # build/weblayout-library/weblayout.library
                                                  # build/weblayout-library/tests/WebLayoutLibTest
WEBLAYOUT=library scripts/build-openbrowser.sh    # GFOpenBrowserLite on LIBS:weblayout.library
```

A program using the library includes `weblayout.h` and then
`<proto/weblayout.h>` (with `-I src/weblayout/library/sdk/Include_H`). It
defines `struct Library *WebLayoutBase`, opens `WEBLAYOUT_NAME` at
`WEBLAYOUT_VERSION` before `WL_Open`, and closes it after `WL_Close`.
`WL_Open` and `WL_Close` stay in the library for the same source to build
both ways. As library calls they only report whether the bases the
library opened are there.

To install, copy `weblayout.library` to `LIBS:`, with gfopenlayout.library 2
(gfopenlayout) beside it.

## Still to do

- Run `WebLayoutLibTest` and GFOpenBrowserLite built with `WEBLAYOUT=library`
  on an AmigaOS 3.2.3 instance, then on the real 68040. Until then Lite's
  default build stays linked and the release archive has no library in it.
- The GFOpenNDK entry (SFD and headers in gfopensdk), and a GFOpenUp part
  for `LIBS:weblayout.library` with gfopenlayout.library beside it.
