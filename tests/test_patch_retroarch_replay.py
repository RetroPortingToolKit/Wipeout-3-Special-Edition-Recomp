from __future__ import annotations

import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "oracle" / "patch_retroarch_replay.py"
SPEC = importlib.util.spec_from_file_location("patch_retroarch_replay", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def make_replay(frame_count: int) -> bytearray:
    header = MODULE.HEADER.pack(
        MODULE.REPLAY_MAGIC,
        MODULE.REPLAY_VERSION,
        0,
        0,
        0,
        0,
        frame_count,
        0,
        0,
        0,
    )
    data = bytearray(header)
    previous_size = 0
    for _ in range(frame_count):
        frame_start = len(data)
        data += struct.pack("<I", previous_size)
        data += b"\x00"
        data += struct.pack("<H", 16)
        for event_id in range(16):
            data += MODULE.INPUT_EVENT.pack(
                0, MODULE.RETRO_DEVICE_JOYPAD, 0, 0, event_id, 0
            )
        data += bytes([MODULE.REGULAR_FRAME])
        previous_size = len(data) - frame_start
    return data


class ReplayPatchTests(unittest.TestCase):
    def test_injects_active_low_route_without_changing_size(self) -> None:
        replay = make_replay(8)
        original_size = len(replay)
        _header, frames = MODULE.parse_frames(replay)
        patched = MODULE.inject_route(
            replay,
            frames,
            [(0xFFF7, 2), (0xFFFF, 1), (0xBFFF, 2)],
            1,
        )
        self.assertEqual(patched, 5 * 16)
        self.assertEqual(len(replay), original_size)

        def value(frame: int, event_id: int) -> int:
            return struct.unpack_from(
                "<h", replay, frames[frame].value_offsets[event_id]
            )[0]

        self.assertEqual(value(0, 3), 0)
        self.assertEqual(value(1, 3), 1)  # Start
        self.assertEqual(value(2, 3), 1)
        self.assertEqual(value(3, 3), 0)
        self.assertEqual(value(4, 0), 1)  # Cross
        self.assertEqual(value(5, 0), 1)
        self.assertEqual(value(6, 0), 0)

    def test_loads_route_and_rejects_out_of_bounds(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            route_path = Path(temporary) / "route.json"
            route_path.write_text(
                json.dumps(
                    {
                        "format": "psxrecomp-input-route-v1",
                        "steps": [{"pad_word": "0xFFBF", "frames": 3}],
                    }
                ),
                encoding="utf-8",
            )
            steps = MODULE.load_route(route_path)
            self.assertEqual(steps, [(0xFFBF, 3)])
            replay = make_replay(3)
            _header, frames = MODULE.parse_frames(replay)
            with self.assertRaisesRegex(MODULE.ReplayFormatError, "beyond replay"):
                MODULE.inject_route(replay, frames, steps, 1)

    def test_rejects_header_frame_count_mismatch(self) -> None:
        replay = make_replay(2)
        struct.pack_into("<I", replay, 24, 3)
        with self.assertRaisesRegex(MODULE.ReplayFormatError, "declares 3 frames"):
            MODULE.parse_frames(replay)


if __name__ == "__main__":
    unittest.main()
