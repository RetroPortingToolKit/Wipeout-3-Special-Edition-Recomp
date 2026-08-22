from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "provenance" / "cook_raw_track.py"
SPEC = importlib.util.spec_from_file_location("cook_raw_track", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
cook_raw_track = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cook_raw_track)


def raw_sector(mode: int, fill: int, *, form2: bool = False) -> bytes:
    sector = bytearray(cook_raw_track.RAW_SECTOR_BYTES)
    sector[:12] = cook_raw_track.SYNC
    sector[15] = mode
    if mode == 1:
        sector[16 : 16 + cook_raw_track.LOGICAL_BLOCK_BYTES] = bytes([fill]) * 2048
    else:
        subheader = bytes((1, 2, 0x20 if form2 else 0, 4))
        sector[16:20] = subheader
        sector[20:24] = subheader
        sector[24 : 24 + 2048] = bytes([fill]) * 2048
    return bytes(sector)


class CookRawTrackTests(unittest.TestCase):
    def test_mode1_and_mode2_forms_preserve_lba_alignment(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "track.bin"
            output = root / "track.iso"
            source.write_bytes(
                raw_sector(1, 0x11)
                + raw_sector(2, 0x22)
                + raw_sector(2, 0x33, form2=True)
            )
            report = cook_raw_track.cook(source, output)
            self.assertEqual(report["sectors"], 3)
            self.assertEqual(
                report["sector_kinds"],
                {"mode1": 1, "mode2_form1": 1, "mode2_form2": 1},
            )
            cooked = output.read_bytes()
            self.assertEqual(len(cooked), 3 * 2048)
            self.assertEqual(cooked[:2048], bytes([0x11]) * 2048)
            self.assertEqual(cooked[2048:4096], bytes([0x22]) * 2048)
            self.assertEqual(cooked[4096:], bytes([0x33]) * 2048)

    def test_invalid_sync_and_subheader_fail_closed(self) -> None:
        sector = bytearray(raw_sector(2, 0x44))
        sector[0] = 1
        with self.assertRaises(cook_raw_track.TrackFormatError):
            cook_raw_track.user_block(bytes(sector), 7)
        sector = bytearray(raw_sector(2, 0x44))
        sector[20] ^= 1
        with self.assertRaises(cook_raw_track.TrackFormatError):
            cook_raw_track.user_block(bytes(sector), 8)

    def test_non_sector_input_and_existing_output_are_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "bad.bin"
            output = root / "out.iso"
            source.write_bytes(b"not a sector")
            with self.assertRaises(cook_raw_track.TrackFormatError):
                cook_raw_track.cook(source, output)
            source.write_bytes(raw_sector(2, 0x55))
            output.write_bytes(b"owned")
            with self.assertRaises(cook_raw_track.TrackFormatError):
                cook_raw_track.cook(source, output)
            self.assertEqual(output.read_bytes(), b"owned")


if __name__ == "__main__":
    unittest.main()
