#!/usr/bin/env python3
"""Focused tests for summarize_wip3_race_clock.py."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parent
FIXTURES = TOOLS / "testdata"
SPEC = importlib.util.spec_from_file_location(
    "summarize_wip3_race_clock", TOOLS / "summarize_wip3_race_clock.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RaceClockSummaryTests(unittest.TestCase):
    def summarize_fixture(self, name: str):
        return MODULE.summarize(
            (FIXTURES / name).read_text(encoding="utf-8"),
            minimum_windows=2,
        )

    def test_realtime_windows_pass(self):
        result = self.summarize_fixture("wip3-race-clock-pass.txt")
        self.assertEqual(result["verdict"], "PASS")
        self.assertEqual(result["schema"], 2)
        self.assertEqual(result["eligible_window_count"], 2)
        self.assertAlmostEqual(result["aggregate_ratio"], 1.0, places=3)
        self.assertTrue(result["aggregate_in_range"])
        self.assertEqual(result["limits"]["min_aggregate_ratio"], 0.99)
        self.assertEqual(result["limits"]["max_aggregate_ratio"], 1.01)

    def test_slow_game_clock_fails(self):
        result = self.summarize_fixture("wip3-race-clock-slow.txt")
        self.assertEqual(result["verdict"], "FAIL")
        self.assertEqual(result["accepted_window_count"], 0)
        self.assertAlmostEqual(result["aggregate_ratio"], 0.4, places=3)

    def test_no_active_race_window_fails_closed(self):
        result = self.summarize_fixture("wip3-race-clock-no-window.txt")
        self.assertEqual(result["verdict"], "FAIL")
        self.assertEqual(result["eligible_window_count"], 0)
        self.assertIn("need 2", result["errors"][0])

    def test_every_window_must_pass_not_only_average(self):
        result = self.summarize_fixture("wip3-race-clock-mixed.txt")
        self.assertEqual(result["verdict"], "FAIL")
        self.assertEqual(result["eligible_window_count"], 2)
        self.assertEqual(result["accepted_window_count"], 1)

    def test_aggregate_must_be_within_one_percent(self):
        # Each window is inside the deliberately wider per-window jitter limit,
        # but their combined in-race timer is 2% slow and must fail closed.
        text = (
            "psxrecomp: wip3 race clock: object=0x80190000 start_cs=100 "
            "end_cs=296 game_seconds=1.960000 wall_seconds=2.000000 "
            "ratio=0.980000 samples=196 zero_steps=98 two_steps=98 "
            "average_step_cs=1.000000 eligible=1\n"
            "psxrecomp: wip3 race clock: object=0x80190000 start_cs=296 "
            "end_cs=492 game_seconds=1.960000 wall_seconds=2.000000 "
            "ratio=0.980000 samples=196 zero_steps=98 two_steps=98 "
            "average_step_cs=1.000000 eligible=1\n"
        )
        result = MODULE.summarize(text, minimum_windows=2)
        self.assertEqual(result["accepted_window_count"], 2)
        self.assertFalse(result["aggregate_in_range"])
        self.assertEqual(result["verdict"], "FAIL")
        self.assertTrue(any("aggregate race-clock ratio" in e for e in result["errors"]))

    def test_aggregate_limits_are_independently_configurable(self):
        text = (
            "psxrecomp: wip3 race clock: object=0x80190000 start_cs=100 "
            "end_cs=296 game_seconds=1.960000 wall_seconds=2.000000 "
            "ratio=0.980000 samples=196 zero_steps=98 two_steps=98 "
            "average_step_cs=1.000000 eligible=1\n"
        ) * 2
        result = MODULE.summarize(
            text,
            minimum_windows=2,
            min_aggregate_ratio=0.97,
            max_aggregate_ratio=1.03,
        )
        self.assertEqual(result["verdict"], "PASS")

    def test_inconsistent_timer_fields_fail(self):
        text = (
            "psxrecomp: wip3 race clock: object=0x80190000 start_cs=100 "
            "end_cs=300 game_seconds=1.000000 wall_seconds=2.000000 "
            "ratio=0.500000 samples=200 zero_steps=100 two_steps=100 "
            "average_step_cs=1.000000 eligible=1\n"
        ) * 2
        result = MODULE.summarize(text, minimum_windows=2)
        self.assertEqual(result["verdict"], "FAIL")
        self.assertFalse(result["windows"][0]["internally_consistent"])

    def test_wrong_per_store_delta_semantics_fail(self):
        text = (
            "psxrecomp: wip3 race clock: object=0x80190000 start_cs=100 "
            "end_cs=500 game_seconds=4.000000 wall_seconds=4.000000 "
            "ratio=1.000000 samples=200 zero_steps=0 two_steps=200 "
            "average_step_cs=2.000000 eligible=1\n"
        ) * 2
        result = MODULE.summarize(text, minimum_windows=2)
        self.assertEqual(result["verdict"], "FAIL")
        self.assertFalse(result["windows"][0]["step_semantics_valid"])


if __name__ == "__main__":
    unittest.main()
