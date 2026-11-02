# GFOpenBrowser Lite 0.4.1

Date: 19 October 2026

0.4.1 brings the Lite 0.2 to 0.4 work (GFOpenLayout rendering, datatype
pictures, the CSS cascade; branch `openlayout-lite`, 7 to 8 October) onto
main, which had moved Lite 0.1 to GFOpenTLS (0.1.1, PR #19). Dale, 19 October:
Lite "should be rescued, modernised and delivered as part of GFOpenUp
standard", and "lite should almost create the bones of a weblayout.library".

## What changed since 0.4

- The program is GFOpenBrowserLite (`$VER: GFOpenBrowserLite 0.4.1`), its window
  "GFOpenBrowser Lite", so that `Version` and GFOpenUp tell it from GFOpenBrowser
  on WebKit. The ARexx port is still AMICRADLE.BROWSER.
- TLS is GFOpenTLS only, main's way (third_party/opentls, GFOpenMail 0.3.3's
  network layer). The branch's own AmiSSL-to-GFOpenTLS swap is not taken. The
  CA list is GFOpenTLS's, `S:GFOpenTLS/ca-bundle.pem`.
- The engine is weblayout, `src/weblayout/`, behind `weblayout.h`
  (`docs/WEBLAYOUT_LIBRARY.md`). `ob_main.c` calls nothing else of it: no
  GFOpenLayout types in the program any more.
- Built with GFOpenAmigaGCC 16.2.0-open3 and GFOpenNDK, for the 68040 with its
  FPU, `-Wall -Wextra -Werror` on everything compiled in (Lite, weblayout,
  GFOpenLayout, GFOpenMail's layer).
- One build script, `scripts/build-openbrowser.sh` (TOOLCHAIN_DIR or STOVE,
  NDK_INCLUDE, OPENLAYOUT_SRC, OPENLAYOUT=static|library, TESTS=1), in
  place of eight; host tests in `tests/run-host-tests.sh`; cloud CI in
  `.github/workflows/lite.yml`.
- The page is clipped to its area: text, pictures and boxes that cross its
  edges no longer draw over the address bar, the scroller or the status
  line.
- Resizing the window keeps the place on the page (it went back to the top).
- Addresses: `wl_url.c` (shared with the HTTP layer's redirects) keeps a
  link's #fragment, tells a scheme from a path that holds "://", and folds
  `.` and `..` at the end of a path; host-tested (`tests/test_wl_url.c`).
- The build no longer needs `/home/...` paths or an AmiSSL SDK.

## Checked

- The host tests (HTML Lite, CSS Lite, addresses) pass, also with
  AddressSanitizer and UBSan.
- GFOpenBrowserLite, obfetch and the four Amiga test programs build with no
  warnings, with GFOpenLayout built in and with the resident
  gfopenlayout.library.

## Not yet checked

- On an AmigaOS 3.2.3 instance and on a real 68040: the window, scrolling,
  the clipping, a click-through, pictures and an https:// page through
  GFOpenTLS. 0.2 to 0.4 were qualified on AC090 with AmiSSL; 0.4.1 is not yet.

## The release archive

`scripts/package-lite.sh` packs a build into
`build/package/GFOpenBrowserLite-0.4.1.lha`: the drawer `GFOpenBrowserLite`
with the program, its icon, the readme and its icon, and the licence, and
the drawer's own icon beside it (the icon set in `icons/`). It writes the
archive's sha256 beside it, and the readme as `GFOpenBrowserLite-0.4.1.readme`
for Aminet. Files are dated from the last commit, so one build packs to the
same bytes every time. The packer is `lha` from Ubuntu's `jlha-utils`
(level 2 headers, which LhA on the Amiga reads). weblayout.library is not
in it: Lite 0.4.1 has the engine built in.

CI (`.github/workflows/lite.yml`) builds the archive on every run, checks
it with lhasa, prints its sha256 in the run's summary and keeps it as the
artifact `openbrowser-lite-release`.

To publish v0.4.1, on a PC with `gh` signed in, once this is merged to
main and the Lite workflow's run on main is green:

```sh
gh run list -R DalsinAI/gfopenbrowser --workflow lite.yml --branch main --limit 1
gh run download <run id> -R DalsinAI/gfopenbrowser -n openbrowser-lite-release -D lite-0.4.1
cd lite-0.4.1
sha256sum -c GFOpenBrowserLite-0.4.1.lha.sha256
gh release create v0.4.1 -R DalsinAI/gfopenbrowser --target main \
    --title "GFOpenBrowser Lite 0.4.1" --notes-file GFOpenBrowserLite-0.4.1.readme \
    GFOpenBrowserLite-0.4.1.lha GFOpenBrowserLite-0.4.1.lha.sha256
```

The asset is then
`https://github.com/DalsinAI/gfopenbrowser/releases/download/v0.4.1/OpenBrowserLite-0.4.1.lha`,
which GFOpenUp pins by that sha256.
