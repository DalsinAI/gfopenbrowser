# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 The AmiCradle Team. MIT licence, see LICENSE.
"""Host self-tests for tools/bench:  python3 -m unittest discover -s tools/bench/tests -v"""
import csv
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
from contextlib import redirect_stdout

BENCH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(BENCH))
sys.path.insert(0, BENCH)

import benchlib  # noqa: E402
import report  # noqa: E402
import stage  # noqa: E402
import test262_slice  # noqa: E402

try:
    import numpy  # noqa: F401
    from PIL import Image
    import compare
    HAVE_IMAGING = True
except ImportError:  # pragma: no cover
    HAVE_IMAGING = False

DAY = 17823  # 19 October 2026, days since 1978

NEW_LOG = """OBBENCH_PAGE id=wiki-amiga set=live target=https://en.wikipedia.org/wiki/Amiga
OBBENCH_RUN view 1
OBSTAMP view-start day=%(day)d ms=36000000 any=690000000 fast=688000000 chip=1900000 largest=600000000
OBVIEW_CREATED 800x600 after 9s
OBVIEW_REQUEST [0 ms] https://en.wikipedia.org/wiki/Amiga
OBVIEW_URL https://en.wikipedia.org/wiki/Amiga
OBVIEW_TITLE Amiga - Wikipedia
OBVIEW_LOADED [2940 ms] https://en.wikipedia.org/wiki/Amiga
OBVIEW_REQUEST [3000 ms] https://en.wikipedia.org/static/x.css
OBVIEW_LOADED [18200 ms] https://en.wikipedia.org/static/x.css FAILED: timed out
OBVIEW_LOADING 0 100%%
OBVIEW_RAN loading=0 invalidations=12 cycles=100 waits=50 at 30s
OBVIEW_PAINTED 800x600
OBVIEW_PNG png/wiki-amiga.png no error has occurred
OBVIEW_BENCH init_ms=9000 load_ms=9100 commit_ms=12040 first_loaded_ms=12050 first_invalidate_ms=12500 load_done_ms=27300 last_resource_ms=27300 painted_ms=28100 end_ms=29000 requests=20 loaded=19 failed=1 bytes=1048576 avail_start=586000000 avail_ready=570000000 avail_min=550000000 avail_painted=552000000 avail_end=580000000 largest_start=500000000
OBVIEW_DONE
OBBENCH_RC view 0
OBSTAMP view-end day=%(day)d ms=36031000 any=689000000 fast=687000000 chip=1900000 largest=600000000
OBBENCH_RUN fetch 1
OBSTAMP fetch-start day=%(day)d ms=36032000 any=690000000 fast=688000000 chip=1900000 largest=600000000
OBBENCH_RC fetch 0
OBSTAMP fetch-end day=%(day)d ms=36036500 any=690000000 fast=688000000 chip=1900000 largest=600000000
FETCH_OK status=200 type=text/html charset=utf-8 bytes=250000 url=https://en.wikipedia.org/wiki/Amiga
OBBENCH_END id=wiki-amiga
OBBENCH_HOST wall_s=95
""" % {"day": DAY}

# An obcore-view from before OBVIEW_BENCH, as on the first lab run.
OLD_LOG = """OBBENCH_PAGE id=example set=live target=https://example.com/
OBBENCH_RUN view 1
OBVIEW_CREATED 800x600 after 9s
OBVIEW_REQUEST [0 ms] https://example.com/
OBVIEW_LOADED [1820 ms] https://example.com/
OBVIEW_LOADING 0 100%
OBVIEW_RAN loading=0 invalidations=3 cycles=10 waits=5 at 13s
OBVIEW_DONE
OBBENCH_RC view 0
OBBENCH_END id=example
"""

FEATURES_LOG = """OBBENCH_PAGE id=features set=compat target=tests/bench/compat/features.html
OBBENCH_RUN view 1
OBVIEW_CONSOLE OBBENCH_FEATURES {"pass":2,"total":3,"missing":["css.sel-has"],"results":{"css.flex":true,"css.grid":true,"css.sel-has":false}} (file:///page.html:170)
OBVIEW_TEXT 120 Acid3 97/100 To pass the test
OBVIEW_CONSOLE JSBENCH int-loop 1200 (12345) (file:///page.html:9)
OBVIEW_CONSOLE JSBENCH calls 800 (1) (file:///page.html:9)
OBBENCH_RC view 0
OBBENCH_TIMEOUT host_s=900
"""


def slurp(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


class Manifest(unittest.TestCase):
    def test_repository_manifest(self):
        pages = benchlib.load_manifest()
        ids = [p["id"] for p in pages]
        self.assertEqual(len(ids), len(set(ids)))
        live = [p for p in pages if p["set"] == "live"]
        self.assertGreaterEqual(len(live), 20)
        self.assertTrue(all(p["target"].startswith("https://") for p in live))
        for p in pages:
            if not p["url"]:
                self.assertTrue(os.path.isfile(os.path.join(ROOT, p["target"])), p["target"])
        acid3 = next(p for p in pages if p["id"] == "acid3")
        self.assertTrue(acid3["text"])
        self.assertFalse(acid3["lite"])
        self.assertEqual(acid3["wait"], 180)
        self.assertEqual(acid3["settle"], 0)

    def test_bad_lines(self):
        for text in ("a local", "a nowhere x.html", "a live tests/x.html", "a b c d e", "bad_id local x.html",
                     "a local x.html wait=x", "a local x.html settle=x", "a local x.html\na local y.html"):
            with self.assertRaises(benchlib.ManifestError, msg=text):
                benchlib.parse_manifest(text)

    def test_comments_and_fragments(self):
        pages = benchlib.parse_manifest("# c\nx live https://a.example/p#frag   # comment\n")
        self.assertEqual(pages[0]["target"], "https://a.example/p#frag")

    def test_amiga_quote(self):
        self.assertEqual(benchlib.amiga_quote('a*b"c'), '"a**b*"c"')
        with self.assertRaises(ValueError):
            benchlib.amiga_quote("a\nb")

    def test_local_pages_are_ours(self):
        for name in os.listdir(os.path.join(ROOT, "tests", "bench", "local")) + ["../compat/features.html"]:
            with open(os.path.join(ROOT, "tests", "bench", "local", name), encoding="utf-8") as f:
                text = f.read()
            self.assertIn("The AmiCradle Team", text, name)
            self.assertNotRegex(text, r'(src|href)="(https?:)?//', name)  # no network in the local set


class Stage(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        os.environ["OS32_GCC16"] = os.path.join(self.tmp, "no-stove")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def stage(self, *args):
        with redirect_stdout(io.StringIO()):
            return stage.main(["--out", os.path.join(self.tmp, "Bench")] + list(args))

    def read(self, *parts):
        with open(os.path.join(self.tmp, "Bench", *parts), encoding="latin-1") as f:
            return f.read()

    def test_pages_only(self):
        self.assertEqual(self.stage("--no-programs"), 0)
        main = self.read("OBBench")
        for p in benchlib.load_manifest():
            self.assertIn("Execute jobs/%s\n" % p["id"], main)
            job = self.read("jobs", p["id"])
            self.assertTrue(job.startswith("; GFOpenBrowser benchmark job"))
            self.assertIn("obstamp view-start >>logs/%s.log" % p["id"], job)
            self.assertIn("OBBENCH_END id=%s" % p["id"], job)
            self.assertNotIn("obfetch", job)
            self.assertTrue(all(len(line) < 400 for line in job.splitlines()))
            job.encode("ascii")
        self.assertIn('obcore-view -wait 180 -settle 0 -text -url "http://acid3.acidtests.org/" 800 600 png/acid3.png', self.read("jobs", "acid3"))
        self.assertIn('obcore-view -wait 120 "pages/text.html" 800 600 png/text.png', self.read("jobs", "text"))
        self.assertTrue(os.path.isfile(os.path.join(self.tmp, "Bench", "pages", "features.html")))
        self.assertEqual(len(self.read("bench-manifest.txt").splitlines()), len(benchlib.load_manifest()))

    def test_with_programs_and_lite(self):
        bin_dir, fonts, lite = (os.path.join(self.tmp, n) for n in ("bin", "fonts", "lite"))
        write(os.path.join(bin_dir, "obcore-view"), "view")
        write(os.path.join(self.tmp, "engine-fingerprint"), "231d9aeeb64ac959\n")
        write(os.path.join(fonts, "LiberationSans-Regular.ttf"), "font")
        write(os.path.join(lite, "obfetch"), "fetch")
        write(os.path.join(lite, "GFOpenBrowserLite"), "lite")
        write(os.path.join(self.tmp, "obstamp"), "stamp")
        write(os.path.join(self.tmp, "gfopenfont.library"), "openfont")
        self.assertEqual(self.stage("--bin", bin_dir, "--fonts", fonts, "--lite", lite, "--obstamp",
                                    os.path.join(self.tmp, "obstamp"), "--openfont", os.path.join(self.tmp, "gfopenfont.library"),
                                    "--sets", "live", "--pages", "example,hn",
                                    "--repeat", "2", "--lite-gui", "15"), 0)
        job = self.read("jobs", "example")
        self.assertEqual(job.count("obcore-view -wait"), 2)
        self.assertEqual(job.count('obfetch "https://example.com/" >T:obbench-fetch.txt'), 2)
        self.assertIn('Run >NIL: GFOpenBrowserLite "https://example.com/"', job)
        self.assertIn("Wait 15", job)
        info = self.read("stage-info.txt")
        self.assertIn("engine-fingerprint 231d9aeeb64ac959", info)
        self.assertIn("obfetch " + hashlib.sha256(b"fetch").hexdigest(), info)
        self.assertIn("gfopenfont.library " + hashlib.sha256(b"openfont").hexdigest(), info)
        for name in ("obcore-view", "obstamp", "obfetch", "Fonts/LiberationSans-Regular.ttf", "Libs/gfopenfont.library", "blocklist"):
            self.assertTrue(os.path.isfile(os.path.join(self.tmp, "Bench", name)), name)
        self.assertEqual(self.read("OBBench").count("Execute jobs/"), 2)

    def test_unknown_page(self):
        with self.assertRaises(SystemExit):
            with redirect_stdout(io.StringIO()), unittest.mock.patch("sys.stderr", io.StringIO()):
                self.stage("--no-programs", "--pages", "nosuchpage")


class Logs(unittest.TestCase):
    def test_new_log(self):
        log = benchlib.parse_log(NEW_LOG)
        self.assertEqual((log["id"], log["set"]), ("wiki-amiga", "live"))
        self.assertEqual(log["host_wall_s"], 95)
        m = benchlib.view_metrics(log["runs"]["view"][0])
        self.assertEqual(m["startup_ms"], 9000)
        self.assertEqual(m["first_response_ms"], 2940)
        self.assertEqual(m["first_paint_ms"], 3400)
        self.assertEqual(m["load_done_ms"], 18200)
        self.assertEqual(m["painted_ms"], 19000)
        self.assertEqual(m["wall_ms"], 31000)
        self.assertEqual((m["requests"], m["failed"], m["bytes"]), (20, 1, 1048576))
        self.assertEqual(m["mem_engine"], 16000000)
        self.assertEqual(m["mem_peak"], 36000000)
        self.assertEqual(m["mem_kept"], 1000000)
        self.assertEqual(m["mem_image"], 104000000)
        self.assertEqual(m["rc"], 0)
        self.assertEqual(m["title"], "Amiga - Wikipedia")
        self.assertTrue(m["done"])
        f = benchlib.fetch_metrics(log["runs"]["fetch"][0])
        self.assertEqual((f["ms"], f["bytes"], f["status"], f["rc"], f["mem_kept"]), (4500, 250000, 200, 0, 0))

    def test_old_log_fallback(self):
        m = benchlib.view_metrics(benchlib.parse_log(OLD_LOG)["runs"]["view"][0])
        self.assertEqual(m["first_response_ms"], 1820)
        self.assertEqual(m["requests"], 1)
        self.assertEqual(m["wall_ms"], 13000)
        self.assertNotIn("mem_peak", m)

    def test_features_acid3_js(self):
        log = benchlib.parse_log(FEATURES_LOG)
        self.assertTrue(log["timeout"])
        m = benchlib.view_metrics(log["runs"]["view"][0])
        self.assertEqual((m["features_pass"], m["features_total"]), (2, 3))
        self.assertEqual(m["features_missing"], ["css.sel-has"])
        self.assertEqual(m["acid3"], 97)
        self.assertEqual(m["js_total_ms"], 2000)

    def test_midnight_and_median(self):
        a = {"day": DAY, "ms": 86399000}
        b = {"day": DAY + 1, "ms": 1000}
        self.assertEqual(benchlib.stamp_ms(b) - benchlib.stamp_ms(a), 2000)
        c = benchlib.combine([{"rc": 0, "wall_ms": 10, "title": "a"}, {"rc": 5, "wall_ms": 30, "title": "b"},
                              {"rc": 0, "wall_ms": 20, "title": None}])
        self.assertEqual((c["rc"], c["wall_ms"], c["title"], c["runs"]), (5, 20, "b", 3))

    def test_bench_keys_match_obcore_view(self):
        with open(os.path.join(ROOT, "src", "webcore", "obcore_view.c"), encoding="utf-8") as f:
            source = f.read()
        printed = set(re.findall(r"(\w+)=%l[du]", source[source.index('"OBVIEW_BENCH '):]))
        with open(os.path.join(BENCH, "benchlib.py"), encoding="utf-8") as f:
            used = set(re.findall(r'b\.get\("(\w+)"|since\("(\w+)"\)|b\["(\w+)"\]', f.read()))
        used = {k for t in used for k in t if k}
        self.assertTrue(used <= printed, used - printed)
        self.assertIn("avail_min", printed)


class Report(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        r = os.path.join(self.tmp, "run")
        write(os.path.join(r, "logs", "wiki-amiga.log"), NEW_LOG)
        write(os.path.join(r, "logs", "example.log"), OLD_LOG)
        write(os.path.join(r, "logs", "features.log"), FEATURES_LOG)
        write(os.path.join(r, "bench-manifest.txt"), "example live https://example.com/\n"
              "wiki-amiga live https://en.wikipedia.org/wiki/Amiga\nfeatures compat tests/bench/compat/features.html\n")
        write(os.path.join(r, "stage-info.txt"), "commit abc\nengine-fingerprint 231d9aeeb64ac959\n")
        write(os.path.join(self.tmp, "compare.csv"), "id,score,ssim,pixel_match,amiga_size,ref_size,note\n"
              "wiki-amiga,81.5,0.815,0.7,800x600,800x600,\n")
        write(os.path.join(self.tmp, "t262.txt"), "SUITE_START\n== t262_all\nT262_PASS test/a.js\n"
              "T262_FAIL test/b.js: Test262Error\nT262_SUMMARY pass=1 fail=1 total=2 commit=d53ebc8b1931\nrc=0\nSUITE_DONE\n")
        self.run_dir = r

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_report_and_regressions(self):
        out = io.StringIO()
        with redirect_stdout(out):
            rc = report.main([self.run_dir, "--compare", os.path.join(self.tmp, "compare.csv"), "--js", os.path.join(self.tmp, "t262.txt")])
        self.assertEqual(rc, 0)
        with open(os.path.join(self.run_dir, "results.csv"), newline="") as f:
            rows = list(csv.DictReader(f))
        self.assertEqual([r["id"] for r in rows], ["example", "wiki-amiga", "features"])
        wiki = rows[1]
        self.assertEqual((wiki["first_response_ms"], wiki["lite_fetch_ms"], wiki["similarity"]), ("2940", "4500", "81.5"))
        self.assertEqual(rows[2]["timeout"], "1")
        md = slurp(os.path.join(self.run_dir, "summary.md"))
        self.assertIn("| wiki-amiga | live | 0 | 9.0 | 2.9 | 3.4 | 18.2 | 19.0 | 31.0 | 20 | 1024 | 34.3 | 4.5 s | 244 |", md)
        self.assertIn("1 of 2 passed (1 failed), test262 commit d53ebc8b1931", md)
        self.assertIn("features: css.sel-has", md)
        self.assertIn("engine-fingerprint 231d9aeeb64ac959", md)

        # A second run, slower and failing, against the first as baseline.
        second = os.path.join(self.tmp, "run2")
        shutil.copytree(self.run_dir, second)
        os.remove(os.path.join(second, "results.csv"))
        slow = NEW_LOG.replace("load_done_ms=27300", "load_done_ms=40000").replace("OBBENCH_RC view 0", "OBBENCH_RC view 20")
        write(os.path.join(second, "logs", "wiki-amiga.log"), slow)
        os.remove(os.path.join(second, "logs", "example.log"))
        with redirect_stdout(io.StringIO()):
            rc = report.main([second, "--baseline", os.path.join(self.run_dir, "results.csv")])
        self.assertEqual(rc, 3)
        md = slurp(os.path.join(second, "summary.md"))
        self.assertIn("wiki-amiga: rc 20 (was 0)", md)
        self.assertIn("wiki-amiga: load_done_ms 18200 -> 30900 (+70%)", md)
        self.assertIn("Not in this run: example", md)

    def test_empty(self):
        empty = os.path.join(self.tmp, "empty")
        os.makedirs(empty)
        with redirect_stdout(io.StringIO()), unittest.mock.patch("sys.stderr", io.StringIO()):
            self.assertEqual(report.main([empty]), 1)


@unittest.skipUnless(HAVE_IMAGING, "Pillow and NumPy")
class Compare(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.amiga, self.ref = os.path.join(self.tmp, "amiga"), os.path.join(self.tmp, "ref")
        os.makedirs(self.amiga)
        os.makedirs(self.ref)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def page(self, path, shift=0, size=(200, 150), mode="RGB"):
        im = Image.new(mode, size, (255, 255, 255) if mode == "RGB" else (0, 0, 0, 0))
        px = im.load()
        for y in range(20, 60):
            for x in range(10 + shift, 120 + shift):
                px[x, y] = (20, 40, 160) if mode == "RGB" else (20, 40, 160, 255)
        for y in range(80, 84):
            for x in range(10, 180):
                px[x, y] = (0, 0, 0) if mode == "RGB" else (0, 0, 0, 255)
        im.save(path)

    def test_scores(self):
        self.page(os.path.join(self.ref, "same.png"))
        self.page(os.path.join(self.amiga, "same.png"), mode="RGBA")  # transparent outside: put on white
        self.page(os.path.join(self.ref, "moved.png"))
        self.page(os.path.join(self.amiga, "moved.png"), shift=40)
        self.page(os.path.join(self.ref, "size.png"), size=(210, 150))
        self.page(os.path.join(self.amiga, "size.png"))
        self.page(os.path.join(self.amiga, "noref.png"))
        Image.new("RGB", (200, 150), (0, 0, 0)).save(os.path.join(self.amiga, "black.png"))
        self.page(os.path.join(self.ref, "black.png"))
        out = os.path.join(self.tmp, "c.csv")
        with redirect_stdout(io.StringIO()):
            self.assertEqual(compare.main([self.amiga, self.ref, "--csv", out, "--diff", os.path.join(self.tmp, "diff")]), 0)
        with open(out, newline="") as f:
            rows = {r["id"]: r for r in csv.DictReader(f)}
        self.assertEqual(float(rows["same"]["score"]), 100.0)
        self.assertEqual(float(rows["same"]["pixel_match"]), 1.0)
        self.assertLess(float(rows["moved"]["score"]), 95.0)
        self.assertGreater(float(rows["moved"]["score"]), float(rows["black"]["score"]))
        self.assertLess(float(rows["black"]["score"]), 20.0)
        self.assertIn("sizes differ", rows["size"]["note"])
        self.assertEqual(rows["noref"]["note"], "no reference")
        self.assertTrue(os.path.isfile(os.path.join(self.tmp, "diff", "moved-diff.png")))


class Test262(unittest.TestCase):
    def test_pinned_slice(self):
        commit, files = test262_slice.read_slice(test262_slice.DEFAULT_SLICE)
        self.assertRegex(commit, r"^[0-9a-f]{40}$")
        names = [n for _, n in files]
        self.assertIn("harness/assert.js", names)
        self.assertIn("LICENSE", names)
        self.assertGreaterEqual(len([n for n in names if n.startswith("test/")]), 80)
        self.assertTrue(all(re.match(r"^[0-9a-f]{64}$", d) for d, _ in files))
        self.assertEqual(len(names), len(set(names)))

    def test_metadata(self):
        src = "/*---\nflags: [onlyStrict]\nincludes: [compareArray.js]\nfeatures:\n  - Symbol\n  - Proxy\n---*/\n"
        m = test262_slice.metadata(src)
        self.assertEqual((m["flags"], m["includes"], m["features"]), (["onlyStrict"], ["compareArray.js"], ["Symbol", "Proxy"]))
        self.assertFalse(test262_slice.eligible(m))
        self.assertTrue(test262_slice.eligible(test262_slice.metadata("/*---\ndescription: x\n---*/")))
        self.assertFalse(test262_slice.eligible(test262_slice.metadata("/*---\nnegative:\n  phase: parse\n---*/")))
        self.assertFalse(test262_slice.eligible(test262_slice.metadata("/*---\nincludes: [propertyHelper.js]\n---*/")))

    def test_fetch_from_checkout(self):
        tmp = tempfile.mkdtemp()
        try:
            co = os.path.join(tmp, "co")
            files = {"harness/assert.js": "var assert = {};\n", "harness/sta.js": "function Test262Error() {}\n",
                     "harness/compareArray.js": "// ca\n", "LICENSE": "BSD\n",
                     "test/built-ins/a.js": "/*---\nincludes: [compareArray.js]\n---*/\n1;\n",
                     "test/language/b.js": "/*---\n---*/\n2;\n"}
            for n, t in files.items():
                write(os.path.join(co, n), t)
            slice_path = os.path.join(tmp, "slice.txt")
            write(slice_path, "# commit %s\n" % ("a" * 40) + "".join(
                "%s %s\n" % (hashlib.sha256(t.encode()).hexdigest(), n) for n, t in files.items()))
            out = os.path.join(tmp, "out")
            with redirect_stdout(io.StringIO()):
                self.assertEqual(test262_slice.main(["fetch", "--slice", slice_path, "--from-checkout", co, "--out", out, "--standalone"]), 0)
            driver = slurp(os.path.join(out, "t262_all.js"))
            self.assertIn('load("t262_harness.js");', driver)
            self.assertIn('t262Run("t262_0002", "test/language/b.js");', driver)
            self.assertIn("total=2", driver)
            self.assertIn("// ca", slurp(os.path.join(out, "t262_harness.js")))
            self.assertIn('print("T262_PASS test/built-ins/a.js");', slurp(os.path.join(out, "t262s_0001.js")))
            self.assertEqual(slurp(os.path.join(out, "t262-names.txt")), "t262_all\n")
            self.assertTrue(os.path.isfile(os.path.join(out, "LICENSE-test262")))
            # a file that does not match its hash: nothing is written
            write(os.path.join(co, "test/language/b.js"), "changed\n")
            out2 = os.path.join(tmp, "out2")
            with redirect_stdout(io.StringIO()), unittest.mock.patch("sys.stderr", io.StringIO()):
                self.assertEqual(test262_slice.main(["fetch", "--slice", slice_path, "--from-checkout", co, "--out", out2]), 1)
            self.assertFalse(os.path.exists(out2))
        finally:
            shutil.rmtree(tmp)


class Scripts(unittest.TestCase):
    def test_shell_syntax(self):
        for script in ("run-bench.sh",):
            subprocess.check_call(["sh", "-n", os.path.join(BENCH, script)])

    def test_make_refs_command(self):
        sys.path.insert(0, BENCH)
        import importlib
        refs = importlib.import_module("make-refs")
        cmd = refs.chrome_command("/c", "file:///x.html", "/o.png", (800, 600), 5000)
        self.assertIn("--window-size=800,600", cmd)
        self.assertIn("--hide-scrollbars", cmd)
        self.assertEqual(cmd[-1], "file:///x.html")
        self.assertEqual(refs.page_url({"url": False, "target": "tests/bench/local/text.html"}),
                         "file://" + os.path.join(ROOT, "tests/bench/local/text.html"))


if __name__ == "__main__":
    unittest.main()
