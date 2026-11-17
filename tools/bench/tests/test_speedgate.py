#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 The AmiCradle Team. MIT licence, see LICENSE.
"""Host self-tests for tools/bench/speedgate.py."""
import csv
import io
import os
import sys
import tempfile
import unittest
import unittest.mock
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import speedgate  # noqa: E402

HEAD = ["id", "set", "rc", "timeout", "startup_ms", "load_done_ms", "painted_ms"]
BASE = [
    ["text", "local", "0", "0", "300", "1200", "1500"],
    ["table", "local", "0", "0", "300", "2400", "3000"],
    ["dom", "local", "0", "0", "300", "4000", "4400"],
    ["bbc", "live", "0", "0", "300", "7100", "9000"],
]


def write(path, rows, head=HEAD):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(head)
        w.writerows(rows)


class Gate(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.results = os.path.join(self.dir.name, "results.csv")
        self.baseline = os.path.join(self.dir.name, "speed-baseline.csv")
        write(self.results, BASE)
        with redirect_stdout(io.StringIO()):
            self.assertEqual(speedgate.main([self.results, "--update", "--baseline", self.baseline, "--note", "rc3"]), 0)

    def tearDown(self):
        self.dir.cleanup()

    def run_gate(self, rows, *more):
        write(self.results, rows)
        out = io.StringIO()
        with redirect_stdout(out), unittest.mock.patch("sys.stderr", out):
            rc = speedgate.main([self.results, "--baseline", self.baseline] + list(more))
        return rc, out.getvalue()

    def test_update_keeps_the_set_only(self):
        rows = speedgate.read_rows(self.baseline)
        self.assertEqual([r["id"] for r in rows], ["dom", "table", "text"])
        self.assertEqual(rows[0]["painted_ms"], "4400")
        self.assertEqual(rows[0]["note"], "rc3")

    def test_same_numbers_pass(self):
        rc, out = self.run_gate(BASE)
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS, 3 pages", out)

    def test_a_busy_host_passes(self):
        # everything 1.8 times slower: the host's load, not a regression
        rows = [r[:4] + [str(int(int(v) * 1.8)) for v in r[4:]] for r in BASE]
        self.assertEqual(self.run_gate(rows)[0], 0)

    def test_the_missing_allocator_fails(self):
        # pages four times slower, as with the allocator not linked
        rows = [r[:5] + [str(int(v) * 4) for v in r[5:]] for r in BASE]
        rc, out = self.run_gate(rows)
        self.assertEqual(rc, 1, out)
        self.assertIn("dom: painted_ms 17600 ms, baseline 4400 ms (4.0 times", out)
        self.assertIn("6 clear regressions", out)

    def test_small_times_need_the_absolute_change_too(self):
        # startup 300 -> 700 ms is 2.3 times but under 500 ms more: no failure
        rows = [r[:4] + ["700"] + r[5:] for r in BASE]
        self.assertEqual(self.run_gate(rows)[0], 0)
        rows = [r[:4] + ["7000"] + r[5:] for r in BASE]     # the 6.9 s start
        rc, out = self.run_gate(rows)
        self.assertEqual(rc, 1)
        self.assertIn("text: startup_ms 7000 ms, baseline 300 ms", out)

    def test_live_pages_are_not_judged(self):
        rows = [list(r) for r in BASE]
        rows[3][6] = "90000"
        self.assertEqual(self.run_gate(rows)[0], 0)

    def test_a_page_that_did_not_run_fails(self):
        rc, out = self.run_gate(BASE[1:])
        self.assertEqual(rc, 1)
        self.assertIn("text: not in the results", out)
        rows = [list(r) for r in BASE]
        rows[1][3] = "1"
        rc, out = self.run_gate(rows)
        self.assertEqual(rc, 1)
        self.assertIn("table: did not finish", out)
        rows = [list(r) for r in BASE]
        rows[2][6] = ""
        rc, out = self.run_gate(rows)
        self.assertEqual(rc, 1)
        self.assertIn("dom: no painted_ms in the results", out)
        rows[2][6] = "-1"
        self.assertEqual(self.run_gate(rows)[0], 1)

    def test_factor_and_min_ms(self):
        rows = [r[:4] + [str(int(int(v) * 1.8)) for v in r[4:]] for r in BASE]
        self.assertEqual(self.run_gate(rows, "--factor", "1.5")[0], 1)
        self.assertEqual(self.run_gate(rows, "--factor", "1.5", "--min-ms", "100000")[0], 0)
        self.assertEqual(self.run_gate(rows, "--factor", "1.0")[0], 2)

    def test_no_baseline_is_not_a_pass(self):
        os.remove(self.baseline)
        rc, out = self.run_gate(BASE)
        self.assertEqual(rc, 2)
        self.assertIn("no baseline", out)

    def test_no_results_is_not_a_pass(self):
        out = io.StringIO()
        with unittest.mock.patch("sys.stderr", out):
            self.assertEqual(speedgate.main([os.path.join(self.dir.name, "none.csv"), "--baseline", self.baseline]), 2)
        self.assertEqual(self.run_gate(BASE, "--set", "compat")[0], 2)

    def test_update_refuses_unfinished_runs(self):
        write(self.results, [["text", "local", "20", "0", "300", "1200", "1500"]])
        out = io.StringIO()
        with redirect_stdout(out), unittest.mock.patch("sys.stderr", out):
            self.assertEqual(speedgate.main([self.results, "--update", "--baseline", self.baseline]), 2)
        # the old baseline is still there
        self.assertEqual(len(speedgate.read_rows(self.baseline)), 3)


if __name__ == "__main__":
    unittest.main()
