from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "analysis" / "inventory_runtime_variant.py"
SPEC = importlib.util.spec_from_file_location("inventory_runtime_variant", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
inventory_runtime_variant = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = inventory_runtime_variant
SPEC.loader.exec_module(inventory_runtime_variant)


def capture_region(payload: bytes, guest_base: int, pcs: list[int]) -> dict[str, object]:
    return {
        "bytes_sha256": hashlib.sha256(payload).hexdigest(),
        "load_addr": guest_base,
        "size": len(payload),
        "executed_pcs": pcs,
    }


class RuntimeVariantInventoryTests(unittest.TestCase):
    def test_changed_observed_and_unobserved_words_are_distinguished(self) -> None:
        source = bytes(range(64))
        runtime = bytearray(source[16:48])
        runtime[4] ^= 0xFF
        runtime[20] ^= 0xFF
        guest_base = 0x80010000
        report = inventory_runtime_variant.inventory(
            source,
            bytes(runtime),
            source_file_sha256=hashlib.sha256(source).hexdigest(),
            source_offset=16,
            guest_base=guest_base,
            capture_region=capture_region(bytes(runtime), guest_base, [guest_base + 4]),
        )
        self.assertEqual(report["status"], "runtime-variant")
        self.assertEqual(report["delta"]["changed_byte_count"], 2)
        observed = report["observed_execution"]
        self.assertEqual(observed["pc_count"], 1)
        self.assertEqual(observed["changed_pc_count"], 1)
        self.assertEqual(observed["changed_bytes_in_observed_words"], 1)
        self.assertEqual(observed["changed_pcs"][0]["pc"], guest_base + 4)

    def test_identical_slice_and_malformed_capture_fail_closed(self) -> None:
        source = bytes(range(32))
        guest_base = 0x1000
        region = capture_region(source[8:24], guest_base, [guest_base])
        report = inventory_runtime_variant.inventory(
            source,
            source[8:24],
            source_file_sha256=hashlib.sha256(source).hexdigest(),
            source_offset=8,
            guest_base=guest_base,
            capture_region=region,
        )
        self.assertEqual(report["status"], "exact-source")
        prefix = inventory_runtime_variant.inventory(
            source,
            source[8:28],
            source_file_sha256=hashlib.sha256(source).hexdigest(),
            source_offset=8,
            guest_base=guest_base,
            capture_region=capture_region(source[8:28], guest_base, [guest_base]),
            comparison_bytes=16,
        )
        self.assertEqual(prefix["runtime"]["capture_bytes"], 20)
        self.assertEqual(prefix["runtime"]["compared_bytes"], 16)
        bad = dict(region)
        bad["executed_pcs"] = [guest_base + 2]
        with self.assertRaises(inventory_runtime_variant.RuntimeVariantError):
            inventory_runtime_variant.inventory(
                source,
                source[8:24],
                source_file_sha256=hashlib.sha256(source).hexdigest(),
                source_offset=8,
                guest_base=guest_base,
                capture_region=bad,
            )

    def test_runtime_subrange_filters_observed_pcs_and_preserves_identity(self) -> None:
        source = bytes(range(64))
        runtime = bytes(range(16, 48))
        guest_base = 0x80010000
        region = capture_region(
            runtime,
            guest_base,
            [guest_base, guest_base + 8, guest_base + 16, guest_base + 28],
        )
        report = inventory_runtime_variant.inventory(
            source,
            runtime,
            source_file_sha256=hashlib.sha256(source).hexdigest(),
            source_offset=24,
            guest_base=guest_base,
            capture_region=region,
            runtime_offset=8,
            comparison_bytes=16,
        )
        self.assertEqual(report["status"], "exact-source")
        self.assertEqual(report["runtime"]["comparison_offset"], 8)
        self.assertEqual(report["runtime"]["capture_guest_start"], guest_base)
        self.assertEqual(report["runtime"]["guest_start"], guest_base + 8)
        self.assertEqual(report["observed_execution"]["pc_count"], 2)

        with self.assertRaises(inventory_runtime_variant.RuntimeVariantError):
            inventory_runtime_variant.inventory(
                source,
                runtime,
                source_file_sha256=hashlib.sha256(source).hexdigest(),
                source_offset=24,
                guest_base=guest_base,
                capture_region=region,
                runtime_offset=3,
                comparison_bytes=16,
            )

    def test_cli_selects_exact_region_and_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.bin"
            runtime = root / "runtime.bin"
            capture = root / "capture.json"
            output = root / "receipt.json"
            source.write_bytes(bytes(range(64)))
            runtime.write_bytes(bytes(range(16, 48)))
            region = capture_region(runtime.read_bytes(), 0x80010000, [0x80010000])
            capture.write_text(
                json.dumps(
                    {
                        "schema": inventory_runtime_variant.NORMALIZED_CAPTURE_SCHEMA,
                        "status": "valid-untrusted-discovery",
                        "regions": [region],
                    }
                )
            )
            argv = [
                "--source",
                str(source),
                "--source-offset",
                "16",
                "--runtime-region",
                str(runtime),
                "--guest-base",
                "0x80010000",
                "--runtime-offset",
                "0",
                "--normalized-capture",
                str(capture),
                "--json-out",
                str(output),
            ]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(inventory_runtime_variant.main(argv), 0)
            self.assertEqual(json.loads(output.read_text())["status"], "exact-source")
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(inventory_runtime_variant.main(argv), 2)


if __name__ == "__main__":
    unittest.main()
