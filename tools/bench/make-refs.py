#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 The AmiCradle Team. MIT licence, see LICENSE.
"""Reference pictures of the benchmark pages for compare.py, from headless
Chromium on Linux at the same viewport as the Amiga run (800x600 unless
told otherwise), scroll bars hidden.

  make-refs.py --out DIR [--sets local,compat] [--pages id,...] [--size 800x600] [--chrome PATH]

Writes DIR/<id>.png, and says when a picture is not WxH. Chromium is
$CHROME, or the first of Playwright's browsers under
$PLAYWRIGHT_BROWSERS_PATH (default /opt/pw-browsers and
~/.cache/ms-playwright; chromium_headless_shell before chromium), or
chrome-headless-shell/chromium/google-chrome on PATH. Live pages
change by the hour and differ by country and cookie banner: make their
references close in time to the Amiga run, and treat their scores as rough.
Chromium is a reference for layout, not a target: fonts and anti-aliasing
differ, so a perfect score is not expected.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import benchlib  # noqa: E402


def find_chrome():
    if os.environ.get("CHROME"):
        return os.environ["CHROME"]
    roots = [os.environ.get("PLAYWRIGHT_BROWSERS_PATH"), "/opt/pw-browsers", os.path.expanduser("~/.cache/ms-playwright")]
    for root in filter(None, roots):
        # headless_shell first: its viewport is exactly --window-size; full
        # Chromium's new headless mode keeps ~87 pixels for a window's top.
        for pattern in ("chromium_headless_shell-*/chrome-linux*/headless_shell", "chromium-*/chrome-linux*/chrome"):
            found = sorted(glob.glob(os.path.join(root, pattern)))
            if found:
                return found[-1]
    for name in ("chrome-headless-shell", "chromium", "chromium-browser", "google-chrome"):
        if shutil.which(name):
            return shutil.which(name)
    return None


def chrome_command(chrome, url, out, size, wait_ms):
    mode = [] if "headless_shell" in os.path.basename(chrome) or "headless-shell" in os.path.basename(chrome) else ["--headless=new"]
    return [chrome] + mode + ["--no-sandbox", "--disable-gpu", "--hide-scrollbars", "--mute-audio",
            "--no-first-run", "--force-device-scale-factor=1", "--window-size=%d,%d" % size,
            "--virtual-time-budget=%d" % wait_ms, "--screenshot=%s" % out, url]


def page_url(page):
    return page["target"] if page["url"] else "file://" + os.path.join(benchlib.ROOT, page["target"])


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", required=True)
    p.add_argument("--manifest", default=benchlib.DEFAULT_MANIFEST)
    p.add_argument("--sets", default="local,compat")
    p.add_argument("--pages")
    p.add_argument("--size", default="800x600")
    p.add_argument("--chrome")
    p.add_argument("--wait-ms", type=int, default=10000, help="virtual time a page gets to settle (default 10000)")
    a = p.parse_args(argv)
    size = tuple(int(x) for x in a.size.lower().split("x"))
    chrome = a.chrome or find_chrome()
    if not chrome:
        sys.exit("no Chromium found: set CHROME or --chrome")
    sets = set(a.sets.split(","))
    pages = [pg for pg in benchlib.load_manifest(a.manifest) if pg["set"] in sets]
    if a.pages:
        pages = [pg for pg in pages if pg["id"] in a.pages.split(",")]
    os.makedirs(a.out, exist_ok=True)
    failed = 0
    for page in pages:
        out = os.path.join(os.path.abspath(a.out), page["id"] + ".png")
        if os.path.exists(out):
            os.remove(out)
        try:
            subprocess.run(chrome_command(chrome, page_url(page), out, size, a.wait_ms),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120, check=False)
        except subprocess.TimeoutExpired:
            pass
        ok = os.path.exists(out)
        failed += not ok
        note = ""
        if ok:
            try:
                from PIL import Image
                with Image.open(out) as im:
                    if im.size != size:
                        note = "  (%dx%d, not %dx%d: use headless_shell)" % (im.size + size)
            except ImportError:
                pass
        print("%-14s %s%s" % (page["id"], out if ok else "FAILED", note))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
