# Restart: GFOpenBrowser

_Written 6 October 2026 at about 23:55 UTC, while all work is paused on @SacredTrees's word (23:28 UTC). Read this first when work resumes; the newest capsule and the live PR list win if they disagree._

## What this repo is

GFOpenBrowser: WebKit (curl port) and JavaScriptCore on a 68040, plus webbrowser.library, which lets any program fetch a page's picture, text and title from one resident engine.

## Where it stands

GFOpenBrowser 0.5 is packed on the home PC (~/AmiCradle-dev/dist/GFOpenBrowser-0.5-68040.lha) and installed on the bench, Instance-24. The hand-written 68k JavaScript interpreter is on by default (WebKit patch 0015, slow steps 9-22x faster). The visit cache (patch 0018) is on a branch, not yet on main. Work goes straight to main as commits. Open PR #5 brings the new icon set.

## Merged lately

- #4 (578cdd5, 2026-10-06): Credit who made GFOpenBrowser: CONTRIBUTORS.md
- #1 (fb84988, 2026-10-06): Say "we" in the repository's text, not a person's name
- #3 (9c87d26, 2026-10-06): GFOpenBrowser: real sites on the 68k, and Nursery offload
- #2 (bd52027, 2026-10-05): GlowIcons-style Workbench icons for GFOpenBrowser

## Open pull requests

- #5 (open): Icons: the new icon set's look, at 64 pixels, with the drawer window sized to them

## Next step

1. Reconnect the JSC session; check the BBC crash repeat and the 68k back-end review; commit patch 0018 if clean.
2. YouTube: an HTMLVideoElement stand-in so the grid builds. Then Google results.
3. Merge #5 (icons) when its owner is back.

## Waiting on @SacredTrees

- The word to resume, and the JSC session reconnected on the home PC.
- Whether 0.5 should go on Instance-23 (unconfirmed).

## Who owns it

JSC thread (runs on the home PC).

## Capsules

Restart capsules for this repo's workstreams, in amigachrome's `capjumps/` shelf:

- [`20261006_AmigaChrome_OpenBrowser_JSC_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)

Team rules that still hold: commits as SacredTrees with no co-author lines; third-party code only on "yes with review" (licence checked, commit and sha256 pinned, fetched at build, never committed); deploys with deploy_dev.py only, on a typed line.
