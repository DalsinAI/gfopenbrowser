#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 The AmiCradle Team. MIT licence, see LICENSE.
"""Stage GFOpenBrowser's benchmark in a folder the Amiga sees as a drawer
(normally DH1:Bench of a test instance): the programs, fonts, the pages of
tests/bench/pages.txt and AmigaDOS scripts that run them.

  stage.py --out DIR [options]

  DIR/OBBench        runs every page in turn ("Execute OBBench" in DIR)
  DIR/jobs/<id>      one page: obcore-view, then GFOpenBrowser Lite's obfetch
                     (live pages, when staged), each between two obstamp
                     lines; the output goes to DIR/logs/<id>.log, the picture
                     to DIR/png/<id>.png
  DIR/bench-manifest.txt  the pages staged, for tools/bench/report.py
  DIR/stage-info.txt      what was staged: commit, engine fingerprint, hashes

Programs (left out with --no-programs, for restaging only the pages):
  obcore-view   from --bin, default $WEBKIT_BUILD/bin or ./build/Amiga/bin;
                stripped with the stove's m68k-amigaos-strip when there is one
  obstamp       --obstamp FILE, or compiled from tools/bench/obstamp.c with
                $OBSTAMP_CC (default $OS32_GCC16/prefix/bin/m68k-amigaos-gcc)
  Fonts/        the .ttf files of --fonts (default $OB_FONTS); needed unless
                DIR/Fonts already has them
  Libs/gfopenfont.library   --openfont FILE (default $OPENFONT_LIB), optional:
                the engine draws its text with gfopenfont.library 1.4 (WebKit
                patch 0090) and opens this one when LIBS: has none or an
                older one
  GFOpenBrowserLite, obfetch   from --lite (default $OB_LITE_DIR), optional

See docs/BENCHMARKS.md.
"""
import argparse
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import benchlib  # noqa: E402

ROOT = benchlib.ROOT


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def job_script(page, opts, have_fetch, lite_gui):
    """The AmigaDOS script for one page, run from the bench drawer."""
    ident = page["id"]
    log = "logs/%s.log" % ident
    wait = page["wait"] or opts.wait
    width, height = opts.size
    target = page["target"] if page["url"] else "pages/%s.html" % ident
    view = ["obcore-view", "-wait", str(wait)]
    if page["settle"] is not None:
        view += ["-settle", str(page["settle"])]
    if page["text"]:
        view.append("-text")
    if page["url"]:
        view.append("-url")
    view += [benchlib.amiga_quote(target), str(width), str(height), "png/%s.png" % ident]
    out = ["; GFOpenBrowser benchmark job %s (staged by tools/bench/stage.py)" % ident,
           "FailAt 1000",
           "Stack 400000",
           "Echo %s >%s" % (benchlib.amiga_quote("OBBENCH_PAGE id=%s set=%s target=%s" % (ident, page["set"], page["target"])), log)]
    for n in range(1, opts.repeat + 1):
        out += ["Echo \"OBBENCH_RUN view %d\" >>%s" % (n, log),
                "obstamp view-start >>%s" % log,
                " ".join(view) + " >>%s" % log,
                "Echo \"OBBENCH_RC view $RC\" >>%s" % log,
                "obstamp view-end >>%s" % log]
    if page["url"] and page["lite"] and have_fetch:
        for n in range(1, opts.repeat + 1):
            out += ["Echo \"OBBENCH_RUN fetch %d\" >>%s" % (n, log),
                    "obstamp fetch-start >>%s" % log,
                    "obfetch %s >T:obbench-fetch.txt" % benchlib.amiga_quote(page["target"]),
                    "Echo \"OBBENCH_RC fetch $RC\" >>%s" % log,
                    "obstamp fetch-end >>%s" % log,
                    "Search T:obbench-fetch.txt FETCH_ NONUM >>%s" % log]
        out.append("Delete T:obbench-fetch.txt QUIET")
    if page["url"] and page["lite"] and lite_gui:
        # Experimental: GFOpenBrowser Lite's window, given lite_gui seconds,
        # then stopped with Ctrl-C (Break), which it takes as Quit.
        out += ["Echo \"OBBENCH_RUN lite 1\" >>%s" % log,
                "obstamp lite-start >>%s" % log,
                "Run >NIL: GFOpenBrowserLite %s" % benchlib.amiga_quote(page["target"]),
                "Wait %d" % lite_gui,
                "obstamp lite-open >>%s" % log,
                "Status >ENV:obbenchcli COMMAND=GFOpenBrowserLite",
                "Break $obbenchcli C",
                "Echo \"OBBENCH_RC lite $RC\" >>%s" % log,
                "Wait 3",
                "obstamp lite-end >>%s" % log,
                "UnSetEnv obbenchcli"]
    out.append("Echo \"OBBENCH_END id=%s\" >>%s" % (ident, log))
    return "\n".join(out) + "\n"


def main_script(pages):
    out = ["; GFOpenBrowser benchmark (tools/bench/stage.py). From this drawer:",
           ";   Execute OBBench",
           "; Each page logs to logs/<id>.log; this script's own log is OBBench.log.",
           "FailAt 1000",
           "Echo \"OBBENCH_START\" >OBBench.log",
           "obstamp bench-start >>OBBench.log"]
    for page in pages:
        out += ["Echo \"OBBENCH_JOB %s\" >>OBBench.log" % page["id"],
                "Execute jobs/%s" % page["id"]]
    out += ["obstamp bench-end >>OBBench.log", "Echo \"OBBENCH_DONE\" >>OBBench.log"]
    return "\n".join(out) + "\n"


def stove_tool(name):
    stove = os.environ.get("OS32_GCC16", os.path.expanduser("~/AmiCradle/stoves/os32-gcc16"))
    path = os.path.join(stove, "prefix", "bin", name)
    return path if os.path.exists(path) else None


def copy_program(src, dst, strip=True):
    strip_tool = stove_tool("m68k-amigaos-strip") if strip else None
    if strip_tool:
        subprocess.check_call([strip_tool, "-o", dst, src])
    else:
        shutil.copyfile(src, dst)
    os.chmod(dst, 0o755)


def stage_programs(opts, out, info):
    bin_dir = opts.bin or os.path.join(os.environ.get("WEBKIT_BUILD", os.path.join(os.getcwd(), "build", "Amiga")), "bin")
    view = os.path.join(bin_dir, "obcore-view")
    if not os.path.isfile(view):
        raise SystemExit("no obcore-view in %s (set --bin or WEBKIT_BUILD)" % bin_dir)
    copy_program(view, os.path.join(out, "obcore-view"))
    info.append("obcore-view %s (built %s)" % (sha256_file(view), time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(os.path.getmtime(view)))))
    fingerprint = os.path.join(os.path.dirname(bin_dir), "engine-fingerprint")
    if os.path.isfile(fingerprint):
        with open(fingerprint) as f:
            info.append("engine-fingerprint %s" % f.read().strip())

    stamp = os.path.join(out, "obstamp")
    if opts.obstamp:
        shutil.copyfile(opts.obstamp, stamp)
    else:
        cc = os.environ.get("OBSTAMP_CC") or stove_tool("m68k-amigaos-gcc")
        if not cc:
            raise SystemExit("no compiler for obstamp: set OBSTAMP_CC or OS32_GCC16, or pass --obstamp")
        flags = os.environ.get("OBSTAMP_CFLAGS", "-m68040 -m68881 -O2 -Wall").split()
        subprocess.check_call([cc] + flags + [os.path.join(benchlib.HERE, "obstamp.c"), "-o", stamp])
    os.chmod(stamp, 0o755)

    fonts = opts.fonts or os.environ.get("OB_FONTS")
    ttf = sorted(glob.glob(os.path.join(fonts, "*.ttf"))) if fonts else []
    os.makedirs(os.path.join(out, "Fonts"), exist_ok=True)
    for f in ttf:
        shutil.copyfile(f, os.path.join(out, "Fonts", os.path.basename(f)))
    if not glob.glob(os.path.join(out, "Fonts", "*.ttf")):
        raise SystemExit("no fonts: set --fonts or OB_FONTS to the Liberation and DejaVu .ttf folder")
    openfont = opts.openfont or os.environ.get("OPENFONT_LIB")
    if openfont:
        os.makedirs(os.path.join(out, "Libs"), exist_ok=True)
        shutil.copyfile(openfont, os.path.join(out, "Libs", "gfopenfont.library"))
        info.append("gfopenfont.library %s" % sha256_file(openfont))
    shutil.copyfile(os.path.join(ROOT, "src", "webcore", "Tata.ads"), os.path.join(out, "Tata.ads"))

    lite = opts.lite or os.environ.get("OB_LITE_DIR")
    have = {}
    for name in ("obfetch", "GFOpenBrowserLite"):
        src = os.path.join(lite, name) if lite else None
        if src and os.path.isfile(src):
            copy_program(src, os.path.join(out, name), strip=False)
            info.append("%s %s" % (name, sha256_file(src)))
            have[name] = True
    return have


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", default=os.environ.get("OB_BENCH_DIR"), help="the bench drawer (default $OB_BENCH_DIR)")
    p.add_argument("--manifest", default=benchlib.DEFAULT_MANIFEST)
    p.add_argument("--sets", default="local,compat,live", help="comma list of sets (default all)")
    p.add_argument("--pages", help="comma list of page ids (default every page of the sets)")
    p.add_argument("--repeat", type=int, default=1, help="runs of each program per page (default 1)")
    p.add_argument("--wait", type=int, default=int(os.environ.get("BENCH_WAIT", "120")), help="obcore-view -wait (default 120)")
    p.add_argument("--size", default="800x600", help="viewport WxH (default 800x600)")
    p.add_argument("--bin", help="folder holding obcore-view")
    p.add_argument("--fonts", help="folder of .ttf files (default $OB_FONTS)")
    p.add_argument("--openfont", help="gfopenfont.library to stage in Libs/ (default $OPENFONT_LIB)")
    p.add_argument("--lite", help="folder holding GFOpenBrowserLite and/or obfetch (default $OB_LITE_DIR)")
    p.add_argument("--obstamp", help="a built obstamp")
    p.add_argument("--lite-gui", type=int, default=0, metavar="SECONDS",
                   help="also open GFOpenBrowserLite's window on live pages for SECONDS (experimental)")
    p.add_argument("--no-programs", action="store_true", help="stage pages and scripts only")
    opts = p.parse_args(argv)
    if not opts.out:
        p.error("--out (or OB_BENCH_DIR) is required")
    try:
        w, h = opts.size.lower().split("x")
        opts.size = (int(w), int(h))
    except ValueError:
        p.error("--size is WxH")
    if opts.repeat < 1:
        p.error("--repeat is 1 or more")
    sets = set(opts.sets.split(","))
    pages = [pg for pg in benchlib.load_manifest(opts.manifest) if pg["set"] in sets]
    if opts.pages:
        wanted = opts.pages.split(",")
        unknown = set(wanted) - {pg["id"] for pg in pages}
        if unknown:
            p.error("no such page in the sets: %s" % ", ".join(sorted(unknown)))
        pages = [pg for pg in pages if pg["id"] in wanted]
    if not pages:
        p.error("no pages selected")

    out = opts.out
    for sub in ("pages", "jobs", "logs", "png"):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    commit = subprocess.run(["git", "-C", ROOT, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    info = ["staged %s" % time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "commit %s" % (commit or "unknown"),
            "size %dx%d repeat %d wait %d" % (opts.size[0], opts.size[1], opts.repeat, opts.wait)]
    if opts.no_programs:
        have = {n: os.path.isfile(os.path.join(out, n)) for n in ("obfetch", "GFOpenBrowserLite")}
    else:
        have = stage_programs(opts, out, info)
    if opts.lite_gui and not have.get("GFOpenBrowserLite"):
        p.error("--lite-gui needs GFOpenBrowserLite in --lite")

    for page in pages:
        if not page["url"]:
            shutil.copyfile(os.path.join(ROOT, page["target"]), os.path.join(out, "pages", page["id"] + ".html"))
        with open(os.path.join(out, "jobs", page["id"]), "w", encoding="latin-1", newline="\n") as f:
            f.write(job_script(page, opts, have.get("obfetch"), opts.lite_gui))
    with open(os.path.join(out, "OBBench"), "w", encoding="latin-1", newline="\n") as f:
        f.write(main_script(pages))
    with open(os.path.join(out, "bench-manifest.txt"), "w", encoding="utf-8") as f:
        for page in pages:
            f.write("%s %s %s\n" % (page["id"], page["set"], page["target"]))
    with open(os.path.join(out, "stage-info.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(info) + "\n")
    print("Staged %d pages in %s (Lite obfetch: %s). On the Amiga, from that drawer: Execute OBBench"
          % (len(pages), out, "yes" if have.get("obfetch") else "no"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
