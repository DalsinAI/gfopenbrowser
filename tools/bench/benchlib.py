# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 The AmiCradle Team. MIT licence, see LICENSE.
"""Shared parts of tools/bench: the page manifest, AmigaDOS quoting and the
parser for the logs an OBBench run leaves (see docs/BENCHMARKS.md)."""
import json
import os
import re
import statistics

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_MANIFEST = os.path.join(ROOT, "tests", "bench", "pages.txt")
SETS = ("local", "compat", "live")
ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9-]{0,29}$")


class ManifestError(ValueError):
    pass


def parse_manifest(text, name="pages.txt"):
    """Pages as dicts: id, set, target, url (bool: a network address), wait,
    settle, text, lite. Raises ManifestError on a bad line or a repeated id."""
    pages, seen = [], set()
    for number, line in enumerate(text.splitlines(), 1):
        line = re.sub(r"(^|\s)#.*$", "", line).strip()   # a comment; "#" inside an address stays
        if not line:
            continue
        fields = line.split()
        if len(fields) < 3:
            raise ManifestError("%s:%d: want: id set target [options]" % (name, number))
        ident, kind, target = fields[:3]
        if not ID.match(ident):
            raise ManifestError("%s:%d: bad id %r (letters, digits, '-', up to 30)" % (name, number, ident))
        if ident in seen:
            raise ManifestError("%s:%d: id %s twice" % (name, number, ident))
        if kind not in SETS:
            raise ManifestError("%s:%d: set %r is not one of %s" % (name, number, kind, ", ".join(SETS)))
        page = {"id": ident, "set": kind, "target": target, "url": bool(re.match(r"^https?://", target)),
                "wait": None, "settle": None, "text": False, "lite": True}
        for option in fields[3:]:
            key, _, value = option.partition("=")
            if key == "wait" and value.isdigit():
                page["wait"] = int(value)
            elif key == "settle" and value.isdigit():
                page["settle"] = int(value)
            elif key == "text" and value in ("0", "1"):
                page["text"] = value == "1"
            elif key == "lite" and value in ("0", "1"):
                page["lite"] = value == "1"
            else:
                raise ManifestError("%s:%d: unknown option %r" % (name, number, option))
        if kind == "live" and not page["url"]:
            raise ManifestError("%s:%d: a live page needs an http(s):// address" % (name, number))
        seen.add(ident)
        pages.append(page)
    return pages


def load_manifest(path=DEFAULT_MANIFEST):
    with open(path, encoding="utf-8") as f:
        return parse_manifest(f.read(), os.path.basename(path))


def amiga_quote(text):
    """A double-quoted AmigaDOS argument: '*' escapes, so '*' becomes '**'
    and '"' becomes '*"'. Line ends are refused."""
    if "\n" in text or "\r" in text:
        raise ValueError("line end in an AmigaDOS argument")
    return '"' + text.replace("*", "**").replace('"', '*"') + '"'


# --- logs -----------------------------------------------------------------

KV = re.compile(r"(\w+)=(-?\d+)")


def _ints(text):
    return {k: int(v) for k, v in KV.findall(text)}


def parse_log(text):
    """One page's log (logs/<id>.log) as a dict: the page line, and per tool
    ("view", "fetch", "lite") a list of runs, each a dict of what the log
    says about it."""
    log = {"id": None, "set": None, "target": None, "runs": {}, "timeout": False, "host_wall_s": None}
    run = None

    def new_run(tool):
        r = {"tool": tool, "stamps": {}, "rc": None, "lines": 0}
        log["runs"].setdefault(tool, []).append(r)
        return r

    for line in text.splitlines():
        line = line.rstrip("\r")
        if line.startswith("OBBENCH_PAGE "):
            m = re.match(r"OBBENCH_PAGE id=(\S+) set=(\S+) target=(.*)$", line)
            if m:
                log["id"], log["set"], log["target"] = m.group(1), m.group(2), m.group(3).strip()
        elif line.startswith("OBBENCH_RUN "):
            parts = line.split()
            run = new_run(parts[1] if len(parts) > 1 else "view")
        elif line.startswith("OBSTAMP "):
            parts = line.split()
            if run is not None and len(parts) > 2:
                run["stamps"][parts[1].rsplit("-", 1)[-1]] = _ints(line)
        elif line.startswith("OBBENCH_RC "):
            parts = line.split()
            if run is not None and len(parts) > 2 and re.match(r"^-?\d+$", parts[2]):
                run["rc"] = int(parts[2])
        elif line.startswith("OBBENCH_TIMEOUT"):
            log["timeout"] = True
        elif line.startswith("OBBENCH_HOST "):
            m = re.search(r"wall_s=(\d+)", line)
            if m:
                log["host_wall_s"] = int(m.group(1))
        elif run is not None:
            run["lines"] += 1
            _parse_tool_line(run, line)
    return log


def _parse_tool_line(run, line):
    s = line.lstrip()
    if s.startswith("OBVIEW_BENCH "):
        run["bench"] = _ints(s)
    elif s.startswith("OBVIEW_REQUEST ") or s.startswith("OBVIEW_LOADED "):
        m = re.match(r"OBVIEW_(REQUEST|LOADED) \[(\d+) ms\] (\S*)(.*)$", s)
        if m:
            key = "requests_seen" if m.group(1) == "REQUEST" else "loaded_seen"
            run.setdefault(key, []).append(int(m.group(2)))
            if m.group(1) == "LOADED" and "FAILED:" in m.group(4):
                run["failed_seen"] = run.get("failed_seen", 0) + 1
    elif s.startswith("OBVIEW_RAN "):
        m = re.search(r"loading=(-?\d+).* at (\d+)s", s)
        if m:
            run["ran_loading"], run["ran_s"] = int(m.group(1)), int(m.group(2))
    elif s.startswith("OBVIEW_TITLE "):
        run["title"] = s[len("OBVIEW_TITLE "):].strip()
    elif s.startswith("OBVIEW_PNG "):
        run["png_ok"] = s.endswith("success") or s.endswith("no error has occurred")
    elif s.startswith("OBVIEW_DONE"):
        run["done"] = True
    elif s.startswith("OBVIEW_ABORT") or s.startswith("OBVIEW_FAIL "):
        run["abort"] = s[:200]
    elif s.startswith("OBVIEW_FAILED "):
        run.setdefault("page_failed", s[len("OBVIEW_FAILED "):][:200])
    elif s.startswith("OBVIEW_TEXT "):
        run["text"] = s[len("OBVIEW_TEXT "):]
    elif s.startswith("OBVIEW_CONSOLE "):
        message = s[len("OBVIEW_CONSOLE "):]
        if message.startswith("OBBENCH_FEATURES "):
            m = re.match(r"OBBENCH_FEATURES (\{.*\})(?: \([^()]*\))?$", message)
            if m:
                try:
                    run["features"] = json.loads(m.group(1))
                except ValueError:
                    pass
        elif message.startswith("JSBENCH "):
            m = re.match(r"JSBENCH (\S+) (\d+) ", message)
            if m:
                run.setdefault("jsbench", {})[m.group(1)] = int(m.group(2))
        elif message.startswith("OBBENCH_JS_DONE "):
            m = re.search(r"total=(\d+)", message)
            if m:
                run["js_total"] = int(m.group(1))
    elif s.startswith("FETCH_OK "):
        v = _ints(s)
        run["fetch_status"], run["fetch_bytes"] = v.get("status"), v.get("bytes")
    elif s.startswith("FETCH_FAIL "):
        run["fetch_error"] = s[len("FETCH_FAIL "):][:200]


def stamp_ms(stamp):
    return stamp["day"] * 86400000 + stamp["ms"]


def _between(stamps, a, b, key):
    if a in stamps and b in stamps and key in stamps[a] and key in stamps[b]:
        return stamps[a][key], stamps[b][key]
    return None


def view_metrics(run):
    """Numbers for one obcore-view run. Times are milliseconds from the
    moment obcore-view asked for the page (OBVIEW_BENCH load_ms); with an
    obcore-view from before OBVIEW_BENCH, from the [ms] of its resource
    lines (counted from the end of its start-up, which is about the same)."""
    m = {}
    b = run.get("bench")
    if b:
        load = b.get("load_ms", -1)

        def since(key):
            v = b.get(key, -1)
            return v - load if v >= 0 and load >= 0 else None
        m["startup_ms"] = b.get("init_ms") if b.get("init_ms", -1) >= 0 else None
        m["first_response_ms"] = since("commit_ms")
        if m["first_response_ms"] is None:
            m["first_response_ms"] = since("first_loaded_ms")
        m["first_paint_ms"] = since("first_invalidate_ms")
        m["load_done_ms"] = since("load_done_ms")
        m["last_resource_ms"] = since("last_resource_ms")
        m["painted_ms"] = since("painted_ms")
        m["requests"] = b.get("requests")
        m["failed"] = b.get("failed")
        m["bytes"] = b.get("bytes") or None
        start = b.get("avail_start", 0)
        if start:
            if b.get("avail_ready"):
                m["mem_engine"] = start - b["avail_ready"]
            if b.get("avail_min"):
                m["mem_peak"] = start - b["avail_min"]
            if b.get("avail_painted"):
                m["mem_painted"] = start - b["avail_painted"]
        m["end_ms"] = b.get("end_ms")
    else:
        loaded = run.get("loaded_seen", [])
        m["first_response_ms"] = loaded[0] if loaded else None
        m["last_resource_ms"] = max(loaded) if loaded else None
        m["load_done_ms"] = m["last_resource_ms"]
        m["requests"] = len(run.get("requests_seen", [])) or None
        m["failed"] = run.get("failed_seen", 0) if loaded else None
        if "ran_s" in run:
            m["end_ms"] = run["ran_s"] * 1000
    st = run.get("stamps", {})
    pair = _between(st, "start", "end", "day")
    if pair:
        m["wall_ms"] = stamp_ms(st["end"]) - stamp_ms(st["start"])
        m["mem_kept"] = st["start"]["any"] - st["end"]["any"]
        if b and b.get("avail_start"):
            m["mem_image"] = st["start"]["any"] - b["avail_start"]
    elif m.get("end_ms") is not None:
        m["wall_ms"] = m["end_ms"]
    m["rc"] = run.get("rc")
    m["title"] = run.get("title")
    m["done"] = bool(run.get("done"))
    if run.get("abort"):
        m["abort"] = run["abort"]
    if "features" in run:
        f = run["features"]
        m["features_pass"], m["features_total"] = f.get("pass"), f.get("total")
        m["features_missing"] = f.get("missing", [])
    if "text" in run:
        a = re.search(r"\b(\d{1,3})\s*/\s*100\b", run["text"])
        if a:
            m["acid3"] = int(a.group(1))
    if "jsbench" in run:
        m["js_total_ms"] = sum(run["jsbench"].values())
    if "js_total" in run:
        m["js_total_ms"] = run["js_total"]
    return m


def fetch_metrics(run):
    m = {"rc": run.get("rc"), "status": run.get("fetch_status"), "bytes": run.get("fetch_bytes"),
         "error": run.get("fetch_error")}
    st = run.get("stamps", {})
    if _between(st, "start", "end", "day"):
        m["ms"] = stamp_ms(st["end"]) - stamp_ms(st["start"])
        m["mem_kept"] = st["start"]["any"] - st["end"]["any"]
    return m


def lite_metrics(run):
    m = {"rc": run.get("rc")}
    st = run.get("stamps", {})
    if _between(st, "start", "open", "day"):
        m["mem_open"] = st["start"]["any"] - st["open"]["any"]
    if _between(st, "start", "end", "day"):
        m["mem_kept"] = st["start"]["any"] - st["end"]["any"]
    return m


def median(values):
    values = [v for v in values if isinstance(v, (int, float)) and not isinstance(v, bool)]
    if not values:
        return None
    v = statistics.median(values)
    return int(v) if float(v).is_integer() else round(v, 1)


def combine(runs):
    """The median of each number over several runs; the last run's other
    values (title, rc of the last run unless one failed: then the worst)."""
    if not runs:
        return {}
    out = {}
    keys = []
    for r in runs:
        keys.extend(k for k in r if k not in keys)
    for k in keys:
        values = [r.get(k) for r in runs]
        numbers = [v for v in values if isinstance(v, (int, float)) and not isinstance(v, bool)]
        if k == "rc":
            rcs = [v for v in values if v is not None]
            out[k] = max(rcs) if rcs else None
        elif numbers and len(numbers) == len([v for v in values if v is not None]):
            out[k] = median(numbers)
        else:
            out[k] = next((v for v in reversed(values) if v is not None), None)
    out["runs"] = len(runs)
    return out
