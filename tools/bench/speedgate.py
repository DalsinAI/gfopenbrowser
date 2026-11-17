#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 The AmiCradle Team. MIT licence, see LICENSE.
"""The speed gate: fail when the fixed pages load clearly slower than the
baseline kept in the repository, so that speed cannot quietly go missing.
(In October 2026 the engine's fast allocator was merged but never linked;
pages loaded four times slower for a month and nothing said so.)

  speedgate.py RESULTS_CSV [--baseline tests/bench/speed-baseline.csv]
               [--factor 2.0] [--min-ms 500] [--set local]
  speedgate.py RESULTS_CSV --update [--baseline FILE] [--note TEXT]

RESULTS_CSV is report.py's results.csv of a benchmark run (best with
--repeat 3: each number is then the median of three runs). Only the pages
of one set are judged, `local` by default: our own pages, no network.

A page fails when one of its times (startup_ms, load_done_ms, painted_ms)
is more than --factor times the baseline's AND more than --min-ms slower.
The lab's times move with the host's load (they can double on a busy
host), so the gate is wide on purpose: it is for a speed-up that went
missing, not for a few per cent; report.py --baseline shows those. A page
of the baseline that is not in the results, did not finish (rc, timeout)
or has no time also fails: a gate that does not run is not a pass.

--update writes the baseline from RESULTS_CSV instead (after a release
candidate's lab run, in a PR of its own with the lab's result).

Exit code: 0 pass, 1 a clear regression, 2 the gate could not run (no
baseline, no results, no page of the set).
"""
import argparse
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_BASELINE = os.path.join(ROOT, "tests", "bench", "speed-baseline.csv")
METRICS = ("startup_ms", "load_done_ms", "painted_ms")
FIELDS = ["id", "set"] + list(METRICS) + ["note"]


def read_rows(path):
    with open(path, newline="", encoding="utf-8") as f:
        return [r for r in csv.DictReader(f) if (r.get("id") or "").strip() and not r["id"].startswith("#")]


def number(value):
    try:
        n = float(value)
    except (TypeError, ValueError):
        return None
    return n if n >= 0 else None


def finished(row):
    return str(row.get("rc", "0") or "0") == "0" and str(row.get("timeout", "0") or "0") == "0"


def judge(results, baseline, page_set="local", factor=2.0, min_ms=500.0):
    """(failures, lines): what failed, and one line per page judged."""
    new = {r["id"]: r for r in results}
    failures, lines = [], []
    pages = [b for b in baseline if (b.get("set") or page_set) == page_set]
    for old in pages:
        pid = old["id"]
        row = new.get(pid)
        if row is None:
            failures.append("%s: not in the results" % pid)
            continue
        if not finished(row):
            failures.append("%s: did not finish (rc %s, timeout %s)" % (pid, row.get("rc", ""), row.get("timeout", "")))
            continue
        parts = []
        for key in METRICS:
            a = number(old.get(key))
            if a is None:
                continue
            b = number(row.get(key))
            if b is None:
                failures.append("%s: no %s in the results (baseline %.0f)" % (pid, key, a))
                continue
            ratio = b / a if a else 0.0
            parts.append("%s %.0f -> %.0f" % (key, a, b))
            if b > a * factor and b - a > min_ms:
                failures.append("%s: %s %.0f ms, baseline %.0f ms (%.1f times; the gate is %.1f times and %.0f ms)"
                                % (pid, key, b, a, ratio, factor, min_ms))
        lines.append("%-10s %s" % (pid, ", ".join(parts)))
    return failures, lines


def update(results, path, page_set, note):
    rows = [r for r in results if (r.get("set") or "") == page_set and finished(r)
            and any(number(r.get(k)) is not None for k in METRICS)]
    if not rows:
        return 0
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS, lineterminator="\n")
        w.writeheader()
        for r in sorted(rows, key=lambda r: r["id"]):
            out = {"id": r["id"], "set": page_set, "note": note}
            for k in METRICS:
                n = number(r.get(k))
                out[k] = "" if n is None else "%.0f" % n
            w.writerow(out)
    return len(rows)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("results")
    p.add_argument("--baseline", default=DEFAULT_BASELINE)
    p.add_argument("--factor", type=float, default=2.0)
    p.add_argument("--min-ms", type=float, default=500.0)
    p.add_argument("--set", dest="page_set", default="local")
    p.add_argument("--update", action="store_true")
    p.add_argument("--note", default="")
    a = p.parse_args(argv)
    if a.factor <= 1.0:
        print("speedgate: --factor must be more than 1", file=sys.stderr)
        return 2
    try:
        results = read_rows(a.results)
    except OSError as e:
        print("speedgate: cannot read the results: %s" % e, file=sys.stderr)
        return 2
    if a.update:
        n = update(results, a.baseline, a.page_set, a.note)
        if not n:
            print("speedgate: no finished '%s' page in %s: baseline not written" % (a.page_set, a.results), file=sys.stderr)
            return 2
        print("speedgate: baseline of %d pages written to %s" % (n, a.baseline))
        return 0
    try:
        baseline = read_rows(a.baseline)
    except OSError as e:
        print("speedgate: no baseline (%s): run a benchmark and --update first" % e, file=sys.stderr)
        return 2
    if not [b for b in baseline if (b.get("set") or a.page_set) == a.page_set]:
        print("speedgate: the baseline has no '%s' page" % a.page_set, file=sys.stderr)
        return 2
    failures, lines = judge(results, baseline, a.page_set, a.factor, a.min_ms)
    for line in lines:
        print(line)
    if failures:
        print("speedgate: FAIL, %d clear regression%s against %s:" % (len(failures), "" if len(failures) == 1 else "s",
                                                                      os.path.basename(a.baseline)))
        for f in failures:
            print("  " + f)
        return 1
    print("speedgate: PASS, %d pages within %.1f times the baseline" % (len(lines), a.factor))
    return 0


if __name__ == "__main__":
    sys.exit(main())
