import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "capture_runtime_baseline",
    ROOT / "tools" / "diagnostics" / "capture_runtime_baseline.py",
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class RuntimeBaselineTests(unittest.TestCase):
    def test_compacts_and_sorts_hot_interpreted_pcs(self):
        ok = {"ok": True}
        responses = {
            "frame": {**ok, "frame": 42},
            "dirty_ram_stats": {
                **ok,
                "insns_run": 99,
                "blocks_run": 50,
                "per_pc": [
                    {"pc": "0x2", "insns": 4, "hits": 1, "entries": 0},
                    {"pc": "0x1", "insns": 9, "hits": 3, "entries": 1},
                ],
            },
            "overlay_loader_status": {
                **ok, "active": 1, "dispatch_interp_fallback": 50,
            },
            "frame_perf": {**ok, "samples": 1, "all": {}, "a4_3": {}},
            "audio_stats": {**ok, "out": {}, "underruns": 0},
            "spu_status": {**ok, "render_frames": 10},
            "cdrom_timing": {**ok, "schedule_exact": 2},
        }

        receipt = MODULE.compact_responses(responses, top_pc_count=1)

        self.assertEqual(receipt["frame"], 42)
        self.assertEqual(receipt["dirty_execution"]["insns_run"], 99)
        self.assertEqual(
            receipt["dirty_execution"]["top_pcs"],
            [{"pc": "0x1", "entries": 1, "hits": 3, "insns": 9}],
        )
        self.assertNotIn("per_pc", receipt["dirty_execution"])

    def test_rejects_failed_response(self):
        responses = {"frame": {"ok": False, "error": "offline"}}
        with self.assertRaisesRegex(RuntimeError, "frame failed"):
            MODULE.compact_responses(responses)


if __name__ == "__main__":
    unittest.main()
