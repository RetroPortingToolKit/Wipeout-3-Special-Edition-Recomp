from __future__ import annotations

import hashlib
import importlib.util
import io
import tempfile
import unittest
from pathlib import Path

import pycdlib

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "provenance" / "inventory_iso9660.py"
SPEC = importlib.util.spec_from_file_location("inventory_iso9660", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
inventory_iso9660 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory_iso9660)


def build_iso(path: Path) -> dict[str, bytes]:
    files = {
        "/BOOT.EXE;1": b"PS-X EXE" + bytes(range(32)),
        "/DATA.BIN;1": b"not executable",
    }
    image = pycdlib.PyCdlib()
    image.new(interchange_level=3, vol_ident="SYNTHETIC")
    for iso_path, payload in files.items():
        image.add_fp(io.BytesIO(payload), len(payload), iso_path=iso_path)
    image.write(str(path))
    image.close()
    return files


class IsoInventoryTests(unittest.TestCase):
    def test_inventory_is_sorted_hashed_and_path_stable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            iso_path = Path(directory) / "fixture.iso"
            files = build_iso(iso_path)
            report = inventory_iso9660.inventory(iso_path)
            self.assertEqual(report["status"], "ok")
            self.assertEqual(report["file_count"], 2)
            self.assertEqual(report["ps_x_exe_files"], ["/BOOT.EXE;1"])
            self.assertEqual(
                [item["iso_path"] for item in report["files"]],
                ["/BOOT.EXE;1", "/DATA.BIN;1"],
            )
            boot = report["files"][0]
            self.assertEqual(boot["logical_path"], "/BOOT.EXE")
            self.assertEqual(boot["hash_status"], "ok")
            self.assertEqual(boot["signature"], "ps-x-exe")
            self.assertEqual(
                boot["sha256"], hashlib.sha256(files["/BOOT.EXE;1"]).hexdigest()
            )
            self.assertNotIn(str(iso_path.parent), str(report))

    def test_hashless_mode_and_missing_input(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            iso_path = root / "fixture.iso"
            build_iso(iso_path)
            report = inventory_iso9660.inventory(iso_path, include_file_hashes=False)
            self.assertNotIn("sha256", report["files"][0])
            self.assertEqual(report["ps_x_exe_files"], [])
            with self.assertRaises(inventory_iso9660.InventoryError):
                inventory_iso9660.inventory(root / "missing.iso")

    def test_selected_extraction_preserves_paths_and_verifies_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            iso_path = root / "fixture.iso"
            files = build_iso(iso_path)
            report = inventory_iso9660.inventory(iso_path)
            output = root / "extracted"
            extracted = inventory_iso9660.extract_selected(
                iso_path, report, output, (".BIN",)
            )
            self.assertEqual(len(extracted), 1)
            self.assertEqual((output / "DATA.BIN").read_bytes(), files["/DATA.BIN;1"])
            with self.assertRaises(inventory_iso9660.InventoryError):
                inventory_iso9660.extract_selected(iso_path, report, output, (".BIN",))

    def test_selected_extraction_rejects_empty_or_unhashed_selection(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            iso_path = root / "fixture.iso"
            build_iso(iso_path)
            report = inventory_iso9660.inventory(iso_path)
            with self.assertRaises(inventory_iso9660.InventoryError):
                inventory_iso9660.extract_selected(
                    iso_path, report, root / "none", (".NOPE",)
                )
            report["files"][1].pop("hash_status")
            with self.assertRaises(inventory_iso9660.InventoryError):
                inventory_iso9660.extract_selected(
                    iso_path, report, root / "bad", (".BIN",)
                )

    def test_extraction_destination_rejects_host_path_syntax(self) -> None:
        root = Path("output")
        self.assertEqual(
            inventory_iso9660.extraction_destination(root, "/DIR/FILE.BIN"),
            root / "DIR" / "FILE.BIN",
        )
        for logical in ("/../escape.bin", "/DIR\\..\\escape.bin", "/C:escape.bin"):
            with self.subTest(logical=logical):
                with self.assertRaises(inventory_iso9660.InventoryError):
                    inventory_iso9660.extraction_destination(root, logical)


if __name__ == "__main__":
    unittest.main()
