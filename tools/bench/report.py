#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 The AmiCradle Team. MIT licence, see LICENSE.
"""Turn an OBBench run's logs into results.csv and a Markdown summary, and
compare them with an earlier results.csv to show what got worse.

  report.py RESULTS_DIR [--compare compare.csv] [--js suite-results.txt]
            [--baseline old/results.csv] [--csv FILE] [--md FILE]

RESULTS_DIR is what tools/bench/run-bench.sh collected (logs/<id>.log,
bench-manifest.txt, stage-info.txt), or the bench drawer itself after
"Execute OBBench" on the Amiga. By default results.csv and summary.md are
written into RESULTS_DIR. --compare adds compare.py's scores, --js the
test262 slice's results (tools/run-js-suite.sh output with T262_ lines).
The exit code is 0, or 3 when --baseline found regressions (for scripts).
"""
import argparse
import csv
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import benchlib  # noqa: E402

FIELDS = ["id", "set", "target", "runs", "rc", "done", "timeout", "title",
          "startup_ms", "first_response_ms", "first_paint_ms", "load_done_ms", "last_resource_ms", "painted_ms", "wall_ms",
          "requests", "failed", "bytes", "mem_image", "mem_engine", "mem_peak", "mem_painted", "mem_kept",
          "lite_fetch_rc", "lite_fetch_status", "lite_fetch_ms", "lite_fetch_bytes", "lite_fetch_mem_kept",
          "lite_gui_rc", "lite_gui_mem_open",
          "features_pass", "features_total", "acid3", "js_total_ms", "similarity", "pixel_match", "host_wall_s", "note"]

# metric: (worse when larger?, relative tolerance, absolute minimum change)
WATCH = {
    "first_response_ms": (True, 0.20, 500), "first_paint_ms": (True, 0.20, 500), "load_done_ms": (True, 0.20, 1000),
    "painted_ms": (True, 0.20, 1000), "wall_ms": (True, 0.20, 1000), "lite_fetch_ms": (True, 0.20, 500),
    "js_total_ms": (True, 0.20, 200), "mem_peak": (True, 0.20, 2 << 20),
    "similarity": (False, 0.0, 5.0), "features_pass": (False, 0.0, 1), "acid3": (False, 0.0, 1),
}


def page_row(log, manifest):
    ident = log["id"]
    row = {k: "" for k in FIELDS}
    row.update(id=ident, set=log.get("set") or manifest.get(ident, ("", ""))[0],
               target=log.get("target") or manifest.get(ident, ("", ""))[1],
               timeout=int(log["timeout"]), host_wall_s=log.get("host_wall_s") or "")
    view = benchlib.combine([benchlib.view_metrics(r) for r in log["runs"].get("view", [])])
    notes = []
    for k, v in view.items():
        if k in row and v is not None and k not in ("id", "set", "target"):
            row[k] = int(v) if isinstance(v, bool) else v
    if view.get("abort"):
        notes.append(view["abort"])
    if view.get("features_missing"):
        notes.append("missing: " + " ".join(view["features_missing"]))
    fetch = benchlib.combine([benchlib.fetch_metrics(r) for r in log["runs"].get("fetch", [])])
    for k, col in (("rc", "lite_fetch_rc"), ("status", "lite_fetch_status"), ("ms", "lite_fetch_ms"),
                   ("bytes", "lite_fetch_bytes"), ("mem_kept", "lite_fetch_mem_kept")):
        if fetch.get(k) is not None:
            row[col] = fetch[k]
    if fetch.get("error"):
        notes.append("obfetch: " + fetch["error"])
    lite = benchlib.combine([benchlib.lite_metrics(r) for r in log["runs"].get("lite", [])])
    if lite.get("rc") is not None:
        row["lite_gui_rc"] = lite["rc"]
    if lite.get("mem_open") is not None:
        row["lite_gui_mem_open"] = lite["mem_open"]
    if log["timeout"]:
        notes.append("timed out on the host")
    row["note"] = "; ".join(notes)[:300]
    return row


def read_manifest(results):
    out = {}
    path = os.path.join(results, "bench-manifest.txt")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 3:
                    out[parts[0]] = (parts[1], parts[2])
    return out


def read_logs(results):
    logs = []
    for path in sorted(glob.glob(os.path.join(results, "logs", "*.log"))):
        with open(path, encoding="latin-1") as f:
            log = benchlib.parse_log(f.read())
        if not log["id"]:
            log["id"] = os.path.basename(path)[:-4]
        logs.append(log)
    return logs


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return {r["id"]: r for r in csv.DictReader(f)}


def number(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def regressions(rows, baseline):
    """Lines saying what got worse (and better) than the baseline."""
    worse, better = [], []
    for row in rows:
        old = baseline.get(row["id"])
        if not old:
            continue
        if str(old.get("rc", "")) == "0" and str(row.get("rc", "")) not in ("", "0"):
            worse.append("%s: rc %s (was 0)" % (row["id"], row["rc"]))
        if str(old.get("timeout", "")) == "0" and str(row.get("timeout", "")) == "1":
            worse.append("%s: timed out (did not before)" % row["id"])
        for key, (larger_worse, rel, absolute) in WATCH.items():
            a, b = number(old.get(key)), number(row.get(key))
            if a is None or b is None:
                continue
            change = b - a
            bad = change > 0 if larger_worse else change < 0
            if abs(change) >= absolute and abs(change) > abs(a) * rel:
                line = "%s: %s %s -> %s (%+.0f%%)" % (row["id"], key, fmt(a), fmt(b), 100.0 * change / a if a else 0)
                (worse if bad else better).append(line)
    gone = sorted(set(baseline) - {r["id"] for r in rows})
    return worse, better, gone


def fmt(v):
    if v is None or v == "":
        return ""
    v = float(v)
    return "%d" % v if v.is_integer() else "%.1f" % v


def secs(ms):
    v = number(ms)
    return "" if v is None else "%.1f" % (v / 1000.0)


def mb(b):
    v = number(b)
    return "" if v is None else "%.1f" % (v / 1048576.0)


def kb(b):
    v = number(b)
    return "" if v is None else "%d" % round(v / 1024.0)


def compat(row):
    parts = []
    if row["features_pass"] != "":
        parts.append("features %s/%s" % (row["features_pass"], row["features_total"]))
    if row["acid3"] != "":
        parts.append("Acid3 %s/100" % row["acid3"])
    if row["js_total_ms"] != "":
        parts.append("JS %s ms" % fmt(row["js_total_ms"]))
    if row["similarity"] != "":
        parts.append("render %s%%" % row["similarity"])
    return ", ".join(parts)


def parse_js(path):
    with open(path, encoding="latin-1") as f:
        text = f.read()
    summary = re.search(r"T262_SUMMARY pass=(\d+) fail=(\d+) total=(\d+)(?: commit=(\w+))?", text)
    fails = re.findall(r"^T262_FAIL (\S+)", text, re.M)
    passes = len(re.findall(r"^T262_PASS ", text, re.M))
    return {"summary": summary.groups() if summary else None, "pass": passes, "fail": fails}


def markdown(rows, info, js, worse, better, gone, baseline_name):
    out = ["# GFOpenBrowser benchmark", ""]
    if info:
        out += ["Staged: " + "; ".join(info), ""]
    out += ["Times in seconds from the moment the page was asked for (wall: the whole program, start-up included); "
            "memory in MB from AvailMem(). Emulator numbers: compare runs on the same instance, not with a real 68040. "
            "See docs/BENCHMARKS.md.", ""]
    out += ["| Page | Set | rc | Start-up | First response | First paint | Load done | Picture | Wall | Requests | KB | Peak MB | "
            "Lite fetch | Lite KB | Compatibility |",
            "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for r in rows:
        rc = "timeout" if str(r["timeout"]) == "1" else str(r["rc"])
        lite = ""
        if r["lite_fetch_ms"] != "" or r["lite_fetch_rc"] != "":
            lite = "%s s" % secs(r["lite_fetch_ms"]) if r["lite_fetch_ms"] != "" else ""
            if str(r["lite_fetch_rc"]) not in ("", "0"):
                lite += " (rc %s)" % r["lite_fetch_rc"]
        out.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            r["id"], r["set"], rc, secs(r["startup_ms"]), secs(r["first_response_ms"]), secs(r["first_paint_ms"]),
            secs(r["load_done_ms"]), secs(r["painted_ms"]), secs(r["wall_ms"]), fmt(r["requests"]), kb(r["bytes"]),
            mb(r["mem_peak"]), lite.strip(), kb(r["lite_fetch_bytes"]), compat(r)))
    ok = sum(1 for r in rows if str(r["rc"]) == "0" and str(r["timeout"]) != "1")
    out += ["", "%d of %d pages ran to the end with rc 0." % (ok, len(rows))]
    if js:
        out += ["", "## JavaScript conformance (test262 slice)", ""]
        if js["summary"]:
            p, f, t, c = js["summary"]
            out.append("%s of %s passed (%s failed), test262 commit %s." % (p, t, f, c or "?"))
        else:
            out.append("%d passed, %d failed (no T262_SUMMARY line: the run stopped early?)." % (js["pass"], len(js["fail"])))
        if js["fail"]:
            out += ["", "Failed: " + ", ".join("`%s`" % x for x in js["fail"][:40]) + (" ..." if len(js["fail"]) > 40 else "")]
    missing = [r for r in rows if "missing:" in r["note"]]
    if missing:
        out += ["", "## Features the probe did not find", ""]
        for r in missing:
            out.append("- %s: %s" % (r["id"], r["note"].split("missing:", 1)[1].split(";")[0].strip()))
    if baseline_name is not None:
        out += ["", "## Against %s" % baseline_name, ""]
        out += ["Worse:"] + (["- " + w for w in worse] or ["- nothing"])
        out += ["", "Better:"] + (["- " + b for b in better] or ["- nothing"])
        if gone:
            out += ["", "Not in this run: " + ", ".join(gone)]
    notes = [r for r in rows if r["note"] and "missing:" not in r["note"]]
    if notes:
        out += ["", "## Notes", ""] + ["- %s: %s" % (r["id"], r["note"]) for r in notes]
    return "\n".join(out) + "\n"


def build(results, compare=None, js_path=None, baseline=None):
    manifest = read_manifest(results)
    order = list(manifest)
    logs = read_logs(results)
    logs.sort(key=lambda l: (order.index(l["id"]) if l["id"] in order else len(order), l["id"]))
    rows = [page_row(l, manifest) for l in logs]
    if compare:
        scores = read_csv(compare)
        for r in rows:
            s = scores.get(r["id"])
            if s and s.get("score") not in (None, ""):
                r["similarity"], r["pixel_match"] = s["score"], s.get("pixel_match", "")
    info = []
    path = os.path.join(results, "stage-info.txt")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            info = [l.strip() for l in f if l.strip()]
    js = parse_js(js_path) if js_path else None
    worse, better, gone = regressions(rows, read_csv(baseline)) if baseline else ([], [], [])
    md = markdown(rows, info, js, worse, better, gone, baseline if baseline else None)
    return rows, md, worse


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("results")
    p.add_argument("--compare")
    p.add_argument("--js")
    p.add_argument("--baseline")
    p.add_argument("--csv")
    p.add_argument("--md")
    a = p.parse_args(argv)
    rows, md, worse = build(a.results, a.compare, a.js, a.baseline)
    if not rows:
        print("no logs in %s/logs" % a.results, file=sys.stderr)
        return 1
    csv_path = a.csv or os.path.join(a.results, "results.csv")
    md_path = a.md or os.path.join(a.results, "summary.md")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)
    with open(md_path, "w", encoding="utf-8") as f:
        f.write(md)
    sys.stdout.write(md)
    print("\nWrote %s and %s" % (csv_path, md_path))
    return 3 if worse else 0


if __name__ == "__main__":
    sys.exit(main())
