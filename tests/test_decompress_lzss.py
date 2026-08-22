from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "provenance" / "decompress_lzss.py"
SPEC = importlib.util.spec_from_file_location("decompress_lzss", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
decompress_lzss = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = decompress_lzss
SPEC.loader.exec_module(decompress_lzss)


def _bits(value: int, width: int) -> list[int]:
    return [(value >> shift) & 1 for shift in range(width - 1, -1, -1)]


def stream(tokens: list[tuple[str, int, int | None]]) -> bytes:
    bits: list[int] = []
    for kind, first, second in tokens:
        if kind == "literal":
            bits.append(1)
            bits.extend(_bits(first, 8))
        elif kind == "match":
            assert second is not None
            bits.append(0)
            bits.extend(_bits(first, decompress_lzss.INDEX_BITS))
            bits.extend(_bits(second, decompress_lzss.LENGTH_BITS))
        else:
            raise AssertionError(f"unknown token kind: {kind}")
    bits.append(0)
    bits.extend([0] * decompress_lzss.INDEX_BITS)
    while len(bits) % 8:
        bits.append(0)
    result = bytearray(len(bits) // 8)
    for index, bit in enumerate(bits):
        result[index // 8] |= bit << (7 - (index % 8))
    return bytes(result)


class LzssDecoderTests(unittest.TestCase):
    def test_literals_and_back_reference_decode_msb_first(self) -> None:
        encoded = stream(
            [
                ("literal", ord("A"), None),
                ("literal", ord("B"), None),
                ("literal", ord("C"), None),
                ("match", 1, 0),
            ]
        )
        decoded, receipt = decompress_lzss.decode(encoded, expected_size=6)
        self.assertEqual(decoded, b"ABCABC")
        self.assertEqual(receipt.literal_tokens, 3)
        self.assertEqual(receipt.match_tokens, 1)
        self.assertLess(receipt.padding_bits, 8)

    def test_overlapping_match_reads_new_window_bytes(self) -> None:
        encoded = stream([("literal", ord("x"), None), ("match", 1, 5)])
        decoded, _ = decompress_lzss.decode(encoded, expected_size=9)
        self.assertEqual(decoded, b"x" * 9)

    def test_malformed_streams_and_bounds_fail_closed(self) -> None:
        with self.assertRaises(decompress_lzss.LzssFormatError):
            decompress_lzss.decode(b"")
        with self.assertRaises(decompress_lzss.LzssFormatError):
            decompress_lzss.decode(stream([("match", 2, 0)]))
        literal = stream([("literal", ord("a"), None)])
        with self.assertRaises(decompress_lzss.LzssFormatError):
            decompress_lzss.decode(literal, expected_size=2)
        with self.assertRaises(decompress_lzss.LzssFormatError):
            decompress_lzss.decode(literal, max_output_bytes=0)
        with self.assertRaises(decompress_lzss.LzssFormatError):
            decompress_lzss.decode(literal, expected_size=1, max_output_bytes=0)

    def test_nonzero_trailing_padding_is_rejected(self) -> None:
        encoded = bytearray(stream([("literal", ord("a"), None)]))
        encoded[-1] |= 1
        with self.assertRaises(decompress_lzss.LzssFormatError):
            decompress_lzss.decode(bytes(encoded), expected_size=1)

    def test_cli_writes_receipt_and_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.bin"
            output = root / "decoded.bin"
            receipt = root / "receipt.json"
            source.write_bytes(stream([("literal", ord("z"), None)]))
            args = [
                "--input",
                str(source),
                "--expected-size",
                "1",
                "--output",
                str(output),
                "--json-out",
                str(receipt),
            ]
            self.assertEqual(decompress_lzss.main(args), 0)
            self.assertEqual(output.read_bytes(), b"z")
            self.assertTrue(receipt.is_file())
            self.assertEqual(decompress_lzss.main(args), 2)


if __name__ == "__main__":
    unittest.main()
