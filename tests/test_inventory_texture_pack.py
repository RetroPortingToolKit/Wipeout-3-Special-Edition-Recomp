from __future__ import annotations

import importlib.util
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "assets" / "inventory_texture_pack.py"
SPEC = importlib.util.spec_from_file_location("inventory_texture_pack", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
inventory_texture_pack = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory_texture_pack)


def _chunk(kind: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(kind)
    crc = zlib.crc32(payload, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def png(width: int = 2, height: int = 3) -> bytes:
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr) + _chunk(b"IEND", b"")


class TexturePackInventoryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "README.md").write_text("pack", encoding="utf-8")
        self.images = self.root / inventory_texture_pack.REPLACEMENT_DIRECTORY
        self.images.mkdir()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write_mappings(self, text: str) -> None:
        (self.root / "Hashes.ini").write_text(text, encoding="utf-8")

    def test_partial_pack_is_explicit_and_png_metadata_is_verified(self) -> None:
        self.write_mappings(
            "[Textures]\n11111111-22222222 = Font/One.png\n"
            "33333333-44444444 = Track/Missing.png\n"
        )
        (self.images / "11111111-22222222.png").write_bytes(png())
        report = inventory_texture_pack.inventory(self.root, "a" * 40)
        self.assertEqual(report["status"], "valid-partial-pack")
        self.assertEqual(report["mapping_count"], 2)
        self.assertEqual(report["texture_count"], 1)
        self.assertEqual(report["missing_mapping_count"], 1)
        self.assertEqual(report["textures"][0]["png"]["width"], 2)
        self.assertEqual(report["textures"][0]["png"]["height"], 3)

    def test_unmapped_file_bad_crc_and_duplicate_key_fail_closed(self) -> None:
        self.write_mappings("11111111-22222222 = Font/One.png\n")
        (self.images / "33333333-44444444.png").write_bytes(png())
        with self.assertRaises(inventory_texture_pack.AssetInventoryError):
            inventory_texture_pack.inventory(self.root, "b" * 40)

        (self.images / "33333333-44444444.png").unlink()
        damaged = bytearray(png())
        damaged[-1] ^= 1
        (self.images / "11111111-22222222.png").write_bytes(damaged)
        with self.assertRaises(inventory_texture_pack.AssetInventoryError):
            inventory_texture_pack.inventory(self.root, "b" * 40)

        self.write_mappings(
            "11111111-22222222 = Font/One.png\n11111111-22222222 = Font/Two.png\n"
        )
        with self.assertRaises(inventory_texture_pack.AssetInventoryError):
            inventory_texture_pack.inventory(self.root, "b" * 40)

    def test_cli_refuses_existing_output(self) -> None:
        self.write_mappings("11111111-22222222 = Font/One.png\n")
        (self.images / "11111111-22222222.png").write_bytes(png())
        output = self.root / "receipt.json"
        argv = [
            "--pack-root",
            str(self.root),
            "--expected-commit",
            "c" * 40,
            "--json-out",
            str(output),
        ]
        self.assertEqual(inventory_texture_pack.main(argv), 0)
        self.assertEqual(json.loads(output.read_text())["texture_count"], 1)
        self.assertEqual(inventory_texture_pack.main(argv), 2)


if __name__ == "__main__":
    unittest.main()
