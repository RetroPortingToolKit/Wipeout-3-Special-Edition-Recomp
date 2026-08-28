#!/usr/bin/env python3

import importlib.util
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "summarize_wip3_progress", HERE / "summarize_wip3_progress.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ProgressSummaryTests(unittest.TestCase):
    def summarize(self, fixture: str):
        text = (HERE / "testdata" / fixture).read_text(encoding="utf-8")
        return MODULE.summarize(text, expected=1.0, tolerance=0.1, minimum_windows=2)

    def test_full_speed_fixture_passes(self):
        self.assertEqual(self.summarize("wip3-progress-pass.txt")["verdict"], "PASS")

    def test_forty_percent_fixture_fails(self):
        self.assertEqual(self.summarize("wip3-progress-slow.txt")["verdict"], "FAIL")

    def test_no_window_fixture_fails_closed(self):
        report = self.summarize("wip3-progress-none.txt")
        self.assertEqual(report["verdict"], "FAIL")
        self.assertEqual(report["eligible_window_count"], 0)


if __name__ == "__main__":
    unittest.main()
