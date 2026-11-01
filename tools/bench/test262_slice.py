#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 The AmigaChrome Team. MIT licence, see LICENSE.
"""A pinned slice of test262 for jsc on the Amiga, fetched at run time.

test262 (https://github.com/tc39/test262, BSD-3-Clause, Ecma International)
is not kept in this repository. tests/bench/test262-slice.txt names one
commit and, for each file of the slice, its path and SHA-256. `fetch`
downloads exactly those files from that commit, checks every hash and
writes, into an output folder (normally DH1:Probe of a test instance):

  t262_harness.js   assert.js, sta.js and the helpers the slice includes
  t262_NNNN.js      one test each, as test262 has it
  t262_all.js       one jsc run for the whole slice: loads the harness, then
                    each test inside try/catch, printing T262_PASS or
                    T262_FAIL lines and a T262_SUMMARY line at the end
  t262-index.txt    NNNN and the test262 path of each test
  t262-names.txt    "t262_all", the name for tools/run-js-suite.sh
  LICENSE-test262   test262's licence, which goes with its files

With --standalone it also writes t262s_NNNN.js (harness + one test + a
T262_PASS line) for one jsc start per test; jsc takes about 30 s to start
on an emulated 68040, so the single driver is the default.

  test262_slice.py fetch --out DIR [--slice FILE] [--from-checkout DIR] [--standalone]
  test262_slice.py select --checkout DIR [--per-dir N] > tests/bench/test262-slice.txt

`select` is for whoever moves the pin: it picks tests from a local
checkout at the pinned commit (no negative, async or module tests, only the
harness helpers listed in HELPERS) and prints the slice file.
"""
import argparse
import hashlib
import os
import re
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_SLICE = os.path.join(ROOT, "tests", "bench", "test262-slice.txt")
RAW = "https://raw.githubusercontent.com/tc39/test262/{commit}/{path}"

# Helpers a selected test may include (besides assert.js and sta.js). Not
# propertyHelper.js: its verifyProperty() deletes the property it checks,
# which would break every later test in the single jsc run.
HELPERS = ("compareArray.js", "isConstructor.js", "fnGlobalObject.js", "nans.js")
SKIP_FLAGS = {"async", "module", "raw", "onlyStrict", "CanBlockIsFalse", "CanBlockIsTrue"}
SKIP_FEATURES = {"Temporal", "ShadowRealm", "decorators", "Atomics", "SharedArrayBuffer", "cross-realm",
                 "IsHTMLDDA", "host-gc-required", "import-defer", "source-phase-imports", "Intl.Locale-info"}
# Folders the slice samples, a few tests from each.
DIRS = (
    "test/built-ins/Array/prototype/map", "test/built-ins/Array/prototype/filter",
    "test/built-ins/Array/prototype/includes", "test/built-ins/Array/prototype/flat",
    "test/built-ins/Array/prototype/sort", "test/built-ins/Array/from",
    "test/built-ins/String/prototype/padStart", "test/built-ins/String/prototype/includes",
    "test/built-ins/String/prototype/replaceAll", "test/built-ins/String/prototype/trim",
    "test/built-ins/Object/entries", "test/built-ins/Object/assign", "test/built-ins/Object/fromEntries",
    "test/built-ins/JSON/parse", "test/built-ins/JSON/stringify",
    "test/built-ins/Math/hypot", "test/built-ins/Number/parseFloat", "test/built-ins/Number/prototype/toFixed",
    "test/built-ins/RegExp/named-groups", "test/built-ins/RegExp/lookBehind",
    "test/built-ins/Map/prototype/set", "test/built-ins/Set/prototype/add",
    "test/built-ins/Symbol/iterator", "test/built-ins/BigInt/asIntN",
    "test/built-ins/Proxy/get", "test/built-ins/Reflect/ownKeys",
    "test/built-ins/Date/prototype/toISOString", "test/built-ins/TypedArrayConstructors/from",
    "test/language/expressions/optional-chaining", "test/language/expressions/coalesce",
    "test/language/expressions/arrow-function", "test/language/expressions/template-literal",
    "test/language/statements/class/subclass", "test/language/statements/for-of",
    "test/language/destructuring/binding", "test/language/rest-parameters",
)
FRONT = re.compile(r"/\*---(.*?)---\*/", re.S)


def metadata(source):
    """The parts of a test's front matter the slice cares about."""
    m = FRONT.search(source)
    text = m.group(1) if m else ""
    def listed(key):
        one = re.search(r"^%s:\s*\[(.*?)\]" % key, text, re.M)
        if one:
            return [x.strip() for x in one.group(1).split(",") if x.strip()]
        block = re.search(r"^%s:\s*\n((?:\s+-\s*.*\n?)+)" % key, text, re.M)
        if block:
            return [line.strip()[1:].strip() for line in block.group(1).splitlines() if line.strip()]
        return []
    return {
        "negative": bool(re.search(r"^negative:", text, re.M)),
        "flags": listed("flags"),
        "includes": listed("includes"),
        "features": listed("features"),
    }


def eligible(meta):
    return (not meta["negative"] and not SKIP_FLAGS & set(meta["flags"])
            and not SKIP_FEATURES & set(meta["features"])
            and all(i in HELPERS for i in meta["includes"]))


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_slice(path):
    commit, files = None, []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.startswith("# commit "):
                commit = line.split()[2]
            elif line and not line.startswith("#"):
                digest, name = line.split(None, 1)
                files.append((digest, name))
    if not commit or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise SystemExit("slice %s names no commit" % path)
    return commit, files


def select(checkout, per_dir, commit):
    out = ["# test262 slice for GFOpenBrowser's jsc runs (tools/bench/test262_slice.py)",
           "# test262: https://github.com/tc39/test262, BSD-3-Clause; fetched at run time, not kept here.",
           "# commit %s" % commit,
           "# sha256 path"]
    for helper in ("assert.js", "sta.js") + HELPERS:
        with open(os.path.join(checkout, "harness", helper), "rb") as f:
            out.append("%s harness/%s" % (sha256(f.read()), helper))
    with open(os.path.join(checkout, "LICENSE"), "rb") as f:
        out.append("%s LICENSE" % sha256(f.read()))
    for d in DIRS:
        folder = os.path.join(checkout, d)
        if not os.path.isdir(folder):
            print("no folder %s" % d, file=sys.stderr)
            continue
        names = sorted(n for n in os.listdir(folder) if n.endswith(".js") and "_FIXTURE" not in n)
        taken = 0
        for name in names:
            with open(os.path.join(folder, name), "rb") as f:
                data = f.read()
            if eligible(metadata(data.decode("utf-8", "replace"))):
                out.append("%s %s/%s" % (sha256(data), d, name))
                taken += 1
                if taken == per_dir:
                    break
    return "\n".join(out) + "\n"


def fetch_one(commit, name, checkout):
    if checkout:
        with open(os.path.join(checkout, name), "rb") as f:
            return f.read()
    with urllib.request.urlopen(RAW.format(commit=commit, path=name), timeout=60) as r:
        return r.read()


def build(out, commit, files, sources, standalone):
    """Writes the Amiga files from the fetched sources (name -> bytes)."""
    os.makedirs(out, exist_ok=True)
    tests = [n for _, n in files if n.startswith("test/")]
    used = set()
    for n in tests:
        used.update(metadata(sources[n].decode("utf-8", "replace"))["includes"])
    harness = b"".join(sources["harness/" + h] + b"\n" for h in ("assert.js", "sta.js") + HELPERS if h in used or h in ("assert.js", "sta.js"))
    with open(os.path.join(out, "t262_harness.js"), "wb") as f:
        f.write(b"// test262 harness, commit " + commit.encode() + b" (BSD-3-Clause, see LICENSE-test262)\n" + harness)
    if "LICENSE" in sources:
        with open(os.path.join(out, "LICENSE-test262"), "wb") as f:
            f.write(sources["LICENSE"])
    index, driver = [], ['// test262 slice driver, commit %s: one jsc run for the whole slice.' % commit,
                         'load("t262_harness.js");',
                         'var t262Pass = 0, t262Fail = 0;',
                         'function t262Run(id, path) {',
                         '    try { load(id + ".js"); t262Pass++; print("T262_PASS " + path); }',
                         '    catch (e) { t262Fail++; print("T262_FAIL " + path + ": " + String(e).slice(0, 200)); }',
                         '}']
    for i, n in enumerate(tests, 1):
        ident = "t262_%04d" % i
        with open(os.path.join(out, ident + ".js"), "wb") as f:
            f.write(sources[n])
        if standalone:
            with open(os.path.join(out, "t262s_%04d.js" % i), "wb") as f:
                f.write(harness + b"\n" + sources[n] + b'\nprint("T262_PASS ' + n.encode() + b'");\n')
        index.append("%04d %s" % (i, n))
        driver.append('t262Run("%s", "%s");' % (ident, n))
    driver.append('print("T262_SUMMARY pass=" + t262Pass + " fail=" + t262Fail + " total=%d commit=%s");' % (len(tests), commit[:12]))
    with open(os.path.join(out, "t262_all.js"), "w", encoding="utf-8") as f:
        f.write("\n".join(driver) + "\n")
    with open(os.path.join(out, "t262-index.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(index) + "\n")
    with open(os.path.join(out, "t262-names.txt"), "w", encoding="utf-8") as f:
        f.write("t262_all\n")
    return len(tests)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("fetch")
    f.add_argument("--out", required=True)
    f.add_argument("--slice", default=DEFAULT_SLICE)
    f.add_argument("--from-checkout", help="a local test262 checkout at the pinned commit, instead of the network")
    f.add_argument("--standalone", action="store_true")
    s = sub.add_parser("select")
    s.add_argument("--checkout", required=True)
    s.add_argument("--commit", required=True)
    s.add_argument("--per-dir", type=int, default=3)
    a = p.parse_args(argv)
    if a.cmd == "select":
        sys.stdout.write(select(a.checkout, a.per_dir, a.commit))
        return 0
    commit, files = read_slice(a.slice)
    sources, bad = {}, []
    for digest, name in files:
        data = fetch_one(commit, name, a.from_checkout)
        if sha256(data) != digest:
            bad.append(name)
        sources[name] = data
    if bad:
        print("SHA-256 mismatch, nothing written: %s" % ", ".join(bad), file=sys.stderr)
        return 1
    n = build(a.out, commit, files, sources, a.standalone)
    print("test262 %s: %d tests in %s (t262_all.js; licence in LICENSE-test262)" % (commit[:12], n, a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
