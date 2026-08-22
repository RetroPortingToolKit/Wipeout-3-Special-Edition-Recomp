from __future__ import annotations

import importlib.util
import io
import json
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "analysis" / "inventory_binary_delta.py"
SPEC = importlib.util.spec_from_file_location("inventory_binary_delta", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
inventory_binary_delta = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory_binary_delta)


class BinaryDeltaInventoryTests(unittest.TestCase):
    def test_exact_byte_and_word_ranges(self) -> None:
        source = bytes(range(16))
        target = bytearray(source)
        target[1] = 99
        target[2] = 98
        target[8] = 97
        report = inventory_binary_delta.inventory(
            source, bytes(target), guest_base=0x80010000
        )
        self.assertEqual(report["changed_byte_count"], 3)
        self.assertEqual(report["changed_byte_range_count"], 2)
        self.assertEqual(report["changed_word_count"], 2)
        self.assertEqual(report["changed_word_range_count"], 2)
        self.assertEqual(report["changed_word_ranges"][1]["guest_start"], 0x80010008)

    def test_identical_and_malformed_images(self) -> None:
        report = inventory_binary_delta.inventory(
            b"\x00" * 8, b"\x00" * 8, guest_base=0x1000
        )
        self.assertEqual(report["status"], "identical")
        cases = [
            (b"", b""),
            (b"\x00" * 4, b"\x00" * 8),
            (b"\x00" * 5, b"\x00" * 5),
        ]
        for source, target in cases:
            with self.assertRaises(inventory_binary_delta.DeltaInventoryError):
                inventory_binary_delta.inventory(source, target, guest_base=0x1000)
        with self.assertRaises(inventory_binary_delta.DeltaInventoryError):
            inventory_binary_delta.inventory(
                b"\x00" * 8, b"\x00" * 8, guest_base=0x1001
            )

    def test_cli_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.bin"
            target = root / "target.bin"
            output = root / "receipt.json"
            source.write_bytes(b"\x00" * 8)
            target.write_bytes(b"\x01" + b"\x00" * 7)
            argv = [
                "--source",
                str(source),
                "--target",
                str(target),
                "--guest-base",
                "0x80010000",
                "--json-out",
                str(output),
            ]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(inventory_binary_delta.main(argv), 0)
            self.assertEqual(json.loads(output.read_text())["status"], "different")
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(inventory_binary_delta.main(argv), 2)


if __name__ == "__main__":
    unittest.main()
