from __future__ import annotations

import base64
import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "analysis" / "normalize_runtime_capture.py"
SPEC = importlib.util.spec_from_file_location("normalize_runtime_capture", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
normalizer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(normalizer)


def _region(start: int, words: int, executed_offsets: list[int]) -> dict[str, object]:
    image = b"".join(index.to_bytes(4, "little") for index in range(words))
    executed = [f"0x{start + offset:08X}" for offset in executed_offsets]
    roots = executed[::2]
    return {
        "schema": normalizer.INPUT_SCHEMA,
        "load_addr": f"0x{start:08X}",
        "size": len(image),
        "bytes_b64": base64.b64encode(image).decode("ascii"),
        "executed_pcs": executed,
        "dispatch_entry_pcs": roots,
        "function_entry_pcs": [],
        "seeds": roots,
    }


def fixture() -> list[dict[str, object]]:
    return [
        _region(0x80001000, 4, [0, 4, 12]),
        _region(0x80002000, 3, [0, 8]),
    ]


class RuntimeCaptureNormalizationTests(unittest.TestCase):
    def test_valid_capture_is_bounded_hashed_and_remains_untrusted(self) -> None:
        report = normalizer.normalize(
            fixture(),
            guest_start=0x80000000,
            guest_end=0x80200000,
            input_sha256="a" * 64,
        )
        self.assertEqual(report["region_count"], 2)
        self.assertEqual(report["total_region_bytes"], 28)
        self.assertEqual(report["total_executed_pc_count"], 5)
        self.assertEqual(report["total_candidate_root_count"], 3)
        self.assertIn("not semantic", report["classification"])
        self.assertEqual(
            report["regions"][0]["bytes_sha256"],
            "baed642339816affb3fe8719792d0e4ce82f12db72b7373d244eaa65445800fe",
        )

    def test_malformed_regions_and_address_sets_fail_closed(self) -> None:
        cases: list[list[dict[str, object]]] = []
        bad_base64 = copy.deepcopy(fixture())
        bad_base64[0]["bytes_b64"] = "not base64!"
        cases.append(bad_base64)
        bad_size = copy.deepcopy(fixture())
        bad_size[0]["size"] = 12
        cases.append(bad_size)
        overlap = copy.deepcopy(fixture())
        overlap[1]["load_addr"] = "0x8000100C"
        cases.append(overlap)
        unsorted = copy.deepcopy(fixture())
        unsorted[0]["executed_pcs"] = ["0x80001004", "0x80001000"]
        cases.append(unsorted)
        outside = copy.deepcopy(fixture())
        outside[0]["executed_pcs"] = ["0x80001010"]
        cases.append(outside)
        unobserved_seed = copy.deepcopy(fixture())
        unobserved_seed[0]["seeds"] = ["0x80001008"]
        cases.append(unobserved_seed)
        extra_key = copy.deepcopy(fixture())
        extra_key[0]["unknown"] = 1
        cases.append(extra_key)
        lowercase_address = copy.deepcopy(fixture())
        lowercase_address[0]["load_addr"] = "0x8000a000"
        cases.append(lowercase_address)

        for document in cases:
            with (
                self.subTest(document=document),
                self.assertRaises(normalizer.CaptureValidationError),
            ):
                normalizer.normalize(
                    document,
                    guest_start=0x80000000,
                    guest_end=0x80200000,
                    input_sha256="b" * 64,
                )

        with self.assertRaises(normalizer.CaptureValidationError):
            normalizer.normalize(
                fixture(),
                guest_start=0x80000000,
                guest_end=0x80200000,
                input_sha256="B" * 64,
            )

    def test_cli_writes_canonical_lists_and_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "capture.json"
            receipt = root / "receipt.json"
            executed = root / "executed.txt"
            roots = root / "roots.txt"
            images = root / "images"
            source.write_text(json.dumps(fixture()), encoding="utf-8")
            args = [
                "--input",
                str(source),
                "--json-out",
                str(receipt),
                "--executed-pcs-out",
                str(executed),
                "--roots-out",
                str(roots),
                "--images-dir",
                str(images),
            ]
            self.assertEqual(normalizer.main(args), 0)
            self.assertEqual(
                executed.read_text(encoding="utf-8"),
                "0x80001000\n0x80001004\n0x8000100C\n0x80002000\n0x80002008\n",
            )
            self.assertEqual(
                roots.read_text(encoding="utf-8"),
                "0x80001000\n0x8000100C\n0x80002000\n",
            )
            image_files = sorted(images.iterdir())
            self.assertEqual(len(image_files), 2)
            self.assertEqual(
                image_files[0].read_bytes(),
                b"\x00\x00\x00\x00\x01\x00\x00\x00\x02\x00\x00\x00\x03\x00\x00\x00",
            )
            self.assertEqual(normalizer.main(args), 2)

    def test_cli_rejects_duplicate_outputs_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "capture.json"
            output = root / "duplicate.out"
            source.write_text(json.dumps(fixture()), encoding="utf-8")
            self.assertEqual(
                normalizer.main(
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
