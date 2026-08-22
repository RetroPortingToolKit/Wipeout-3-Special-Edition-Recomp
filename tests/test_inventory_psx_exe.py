from __future__ import annotations

import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "provenance" / "inventory_psx_exe.py"
SPEC = importlib.util.spec_from_file_location("inventory_psx_exe", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
inventory_psx_exe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory_psx_exe)


def fixture(
    words: list[int],
    *,
    load: int = 0x1000,
    entry_offset: int = 0,
    trailing: bytes = b"",
) -> bytes:
    header = bytearray(inventory_psx_exe.HEADER_BYTES)
    header[:8] = inventory_psx_exe.MAGIC
    struct.pack_into("<I", header, 0x10, load + entry_offset)
    struct.pack_into("<I", header, 0x18, load)
    struct.pack_into("<I", header, 0x1C, len(words) * 4)
    text = b"".join(struct.pack("<I", word) for word in words)
    return bytes(header) + text + trailing


def jal(pc: int, target: int) -> int:
    assert ((pc + 4) & 0xF0000000) == (target & 0xF0000000)
    return (3 << 26) | ((target >> 2) & 0x03FFFFFF)


class PsxExeInventoryTests(unittest.TestCase):
    def test_identity_and_untrusted_call_hints_are_deterministic(self) -> None:
        load = 0x1000
        data = fixture(
            [
                jal(load, load + 0x10),
                0,
                jal(load + 8, load + 0x10),
                0,
                0x03E00008,
                0,
            ],
            load=load,
            trailing=b"pad",
        )
        report = inventory_psx_exe.parse(data, "fixture.exe")
        identity = report["executable_identity"]
        self.assertEqual(
            identity["load_range"], {"start": load, "end_exclusive": load + 24}
        )
        self.assertEqual(len(identity["sha256"]), 64)
        scan = report["control_word_scan"]
        self.assertIn("untrusted", scan["classification"])
        self.assertEqual(
            scan["unique_in_range_call_targets"],
            [{"target": load + 0x10, "occurrences": 2}],
        )
        self.assertEqual(report["trailing"]["bytes"], 3)

    def test_invalid_header_ranges_and_alignment_fail_closed(self) -> None:
        with self.assertRaises(inventory_psx_exe.ExeInventoryError):
            inventory_psx_exe.parse(b"short")
        bad_magic = bytearray(fixture([0]))
        bad_magic[0] = 0
        with self.assertRaises(inventory_psx_exe.ExeInventoryError):
            inventory_psx_exe.parse(bytes(bad_magic))
        bad_entry = bytearray(fixture([0]))
        struct.pack_into("<I", bad_entry, 0x10, 0x2000)
        with self.assertRaises(inventory_psx_exe.ExeInventoryError):
            inventory_psx_exe.parse(bytes(bad_entry))
        unaligned = bytearray(fixture([0]))
        struct.pack_into("<I", unaligned, 0x18, 0x1001)
        with self.assertRaises(inventory_psx_exe.ExeInventoryError):
            inventory_psx_exe.parse(bytes(unaligned))
        truncated = fixture([0])[:-1]
        with self.assertRaises(inventory_psx_exe.ExeInventoryError):
            inventory_psx_exe.parse(truncated)

    def test_address_wrap_is_rejected(self) -> None:
        data = bytearray(fixture([0, 0], load=0xFFFFFFFC))
        struct.pack_into("<I", data, 0x10, 0xFFFFFFFC)
        with self.assertRaises(inventory_psx_exe.ExeInventoryError):
            inventory_psx_exe.parse(bytes(data))

    def test_cli_writes_roots_and_refuses_existing_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "fixture.exe"
            receipt = root / "receipt.json"
            roots = root / "roots.txt"
            text = struct.pack("<II", 0x03E00008, 0)
            source.write_bytes(fixture([0x03E00008, 0]))
            text_output = root / "text.bin"
            args = [
                "--input",
                str(source),
                "--json-out",
                str(receipt),
                "--roots-out",
                str(roots),
                "--text-out",
                str(text_output),
            ]
            self.assertEqual(inventory_psx_exe.main(args), 0)
            self.assertEqual(roots.read_text(encoding="utf-8"), "0x00001000\n")
            self.assertEqual(text_output.read_bytes(), text)
            self.assertEqual(inventory_psx_exe.main(args), 2)

    def test_cli_rejects_duplicate_outputs_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "fixture.exe"
            output = root / "duplicate.out"
            source.write_bytes(fixture([0x03E00008, 0]))
            self.assertEqual(
                inventory_psx_exe.main(
                    [
                        "--input",
                        str(source),
                        "--json-out",
                        str(output),
                        "--roots-out",
                        str(output),
                    ]
                ),
                2,
            )
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
