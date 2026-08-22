from __future__ import annotations

import hashlib
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "provenance" / "inventory_pbp.py"
SPEC = importlib.util.spec_from_file_location("inventory_pbp", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
inventory_pbp = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory_pbp)


def fixture(entries: list[tuple[int, bytes]]) -> bytes:
    count = len(entries)
    cursor = inventory_pbp.align4(
        inventory_pbp.TABLE_OFFSET + count * inventory_pbp.ENTRY_BYTES
    )
    result = bytearray(cursor)
    struct.pack_into("<I", result, inventory_pbp.COUNT_OFFSET, count)
    for index, (tag, payload) in enumerate(entries):
        struct.pack_into(
            "<III",
            result,
            inventory_pbp.TABLE_OFFSET + index * inventory_pbp.ENTRY_BYTES,
            cursor,
            tag,
            len(payload),
        )
        result.extend(payload)
        while len(result) % 4:
            result.append(0)
        cursor = len(result)
    return bytes(result)


def _bits(value: int, width: int) -> list[int]:
    return [(value >> shift) & 1 for shift in range(width - 1, -1, -1)]


def repeated_byte_stream(value: int) -> bytes:
    # One literal followed by an overlapping 8-byte match at window slot 1.
    bits = [1] + _bits(value, 8) + [0] + _bits(1, 13) + _bits(5, 4)
    bits += [0] + [0] * 13
    while len(bits) % 8:
        bits.append(0)
    result = bytearray(len(bits) // 8)
    for index, bit in enumerate(bits):
        result[index // 8] |= bit << (7 - (index % 8))
    return bytes(result)


class PbpInventoryTests(unittest.TestCase):
    def test_table_is_validated_and_entries_are_hashed(self) -> None:
        payloads = [(7, b"abc"), (9, b"012345")]
        parsed = inventory_pbp.parse(fixture(payloads))
        self.assertEqual(len(parsed), 2)
        self.assertEqual(parsed[0]["tag"], 7)
        self.assertEqual(parsed[0]["bytes"], 3)
        self.assertEqual(parsed[0]["sha256"], hashlib.sha256(b"abc").hexdigest())
        self.assertEqual(parsed[1]["prefix_hex"], b"012345".hex())

    def test_overlap_truncation_and_absurd_count_fail_closed(self) -> None:
        data = bytearray(fixture([(1, b"payload")]))
        struct.pack_into("<I", data, inventory_pbp.TABLE_OFFSET, 4)
        with self.assertRaises(inventory_pbp.PbpFormatError):
            inventory_pbp.parse(bytes(data))
        with self.assertRaises(inventory_pbp.PbpFormatError):
            inventory_pbp.parse(bytes(data[:40]))
        data = bytearray(100)
        struct.pack_into("<I", data, inventory_pbp.COUNT_OFFSET, 5000)
        with self.assertRaises(inventory_pbp.PbpFormatError):
            inventory_pbp.parse(bytes(data))

    def test_extraction_refuses_existing_destination(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "fixture.pbp"
            source.write_bytes(fixture([(3, b"first"), (4, b"second")]))
            report, data = inventory_pbp.inventory(source)
            destination = root / "entries"
            inventory_pbp.extract(report, data, destination)
            names = sorted(path.name for path in destination.iterdir())
            self.assertEqual(len(names), 2)
            with self.assertRaises(inventory_pbp.PbpFormatError):
                inventory_pbp.extract(report, data, destination)

    def test_expanded_verification_hashes_stored_and_compressed_entries(self) -> None:
        expanded = b"x" * 9
        compressed = repeated_byte_stream(ord("x"))
        data = fixture([(3, b"raw"), (len(expanded), compressed)])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "fixture.pbp"
            source.write_bytes(data)
            report, _ = inventory_pbp.inventory(source, verify_expanded=True)
        self.assertEqual(report["schema"], "psxrecomp.pbp-inventory.v2")
        self.assertEqual(
            report["expanded_verification"],
            {"status": "exact", "stored_entries": 1, "lzss_entries": 1},
        )
        self.assertEqual(report["entries"][0]["encoding"], "stored")
        self.assertEqual(report["entries"][1]["encoding"], "lzss")
        self.assertEqual(report["entries"][1]["expanded_bytes"], len(expanded))
        self.assertEqual(
            report["entries"][1]["expanded_sha256"],
            hashlib.sha256(expanded).hexdigest(),
        )

    def test_expanded_extraction_reproduces_logical_payloads(self) -> None:
        expanded = b"z" * 9
        compressed = repeated_byte_stream(ord("z"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "fixture.pbp"
            source.write_bytes(fixture([(3, b"raw"), (len(expanded), compressed)]))
            report, data = inventory_pbp.inventory(source, verify_expanded=True)
            destination = root / "expanded"
            inventory_pbp.extract_expanded(report, data, destination)
            payloads = [path.read_bytes() for path in sorted(destination.iterdir())]
            self.assertEqual(payloads, [b"raw", expanded])
            with self.assertRaises(inventory_pbp.PbpFormatError):
                inventory_pbp.extract_expanded(report, data, destination)

    def test_expanded_extraction_requires_verified_report(self) -> None:
        data = fixture([(3, b"raw")])
        report = {"entries": inventory_pbp.parse(data)}
        with (
            tempfile.TemporaryDirectory() as directory,
            self.assertRaises(inventory_pbp.PbpFormatError),
        ):
            inventory_pbp.extract_expanded(report, data, Path(directory) / "expanded")

    def test_expanded_verification_rejects_bad_size_or_stream(self) -> None:
        too_small = fixture([(1, b"stored")])
        report = {"entries": inventory_pbp.parse(too_small)}
        with self.assertRaises(inventory_pbp.PbpFormatError):
            inventory_pbp.verify_expanded_entries(report, too_small)

        malformed = fixture([(20, b"\xff")])
        report = {"entries": inventory_pbp.parse(malformed)}
        with self.assertRaises(inventory_pbp.PbpFormatError):
            inventory_pbp.verify_expanded_entries(report, malformed)

        oversized = fixture(
            [
                (
                    inventory_pbp.MAX_EXPANDED_ENTRY_BYTES + 1,
                    repeated_byte_stream(ord("x")),
                )
            ]
        )
        report = {"entries": inventory_pbp.parse(oversized)}
        with self.assertRaises(inventory_pbp.PbpFormatError):
            inventory_pbp.verify_expanded_entries(report, oversized)


if __name__ == "__main__":
    unittest.main()
