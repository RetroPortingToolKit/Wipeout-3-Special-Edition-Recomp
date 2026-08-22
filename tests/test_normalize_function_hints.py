from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "analysis" / "normalize_function_hints.py"
SPEC = importlib.util.spec_from_file_location("normalize_function_hints", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
normalizer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(normalizer)


def fixture() -> dict[str, object]:
    functions = [
        {
            "entry": 0x1000,
            "name": "first",
            "is_thunk": False,
            "body_bytes": 12,
            "body_ranges": [{"start": 0x1000, "end_exclusive": 0x100C}],
            "instruction_count": 3,
        },
        {
            "entry": 0x1010,
            "name": "second",
            "is_thunk": True,
            "body_bytes": 12,
            "body_ranges": [{"start": 0x1008, "end_exclusive": 0x1014}],
            "instruction_count": 3,
        },
    ]
    return {
        "schema": normalizer.INPUT_SCHEMA,
        "classification": "untrusted analysis hints; not executable coverage",
        "language": "synthetic:LE:32",
        "function_count": 2,
        "instruction_count": 6,
        "functions": functions,
    }


class FunctionHintNormalizationTests(unittest.TestCase):
    def test_valid_hints_are_coalesced_but_remain_untrusted(self) -> None:
        report = normalizer.normalize(
            fixture(),
            image_start=0x1000,
            image_end_exclusive=0x1100,
            input_sha256="a" * 64,
        )
        self.assertEqual(report["candidate_roots"], [0x1000, 0x1010])
        self.assertEqual(
            report["candidate_body_union"],
            [{"start": 0x1000, "end_exclusive": 0x1014}],
        )
        self.assertEqual(report["candidate_body_union_bytes"], 20)
        self.assertIn("not executable coverage", report["classification"])

    def test_bad_counts_order_ranges_and_entries_fail_closed(self) -> None:
        cases: list[dict[str, object]] = []
        bad_count = fixture()
        bad_count["instruction_count"] = 5
        cases.append(bad_count)
        bad_order = fixture()
        bad_order["functions"] = list(reversed(bad_order["functions"]))
        cases.append(bad_order)
        bad_range = fixture()
        bad_range["functions"][0]["body_ranges"][0]["end_exclusive"] = 0x100A
        cases.append(bad_range)
        bad_entry = fixture()
        bad_entry["functions"][0]["entry"] = 0x1200
        cases.append(bad_entry)
        for document in cases:
            with self.subTest(document=document):
                with self.assertRaises(normalizer.HintValidationError):
                    normalizer.normalize(
                        document,
                        image_start=0x1000,
                        image_end_exclusive=0x1100,
                        input_sha256="b" * 64,
                    )
        with self.assertRaises(normalizer.HintValidationError):
            normalizer.normalize(
                fixture(),
                image_start=0x1000,
                image_end_exclusive=0x1100,
                input_sha256="A" * 64,
            )

    def test_cli_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.json"
            receipt = root / "receipt.json"
            roots = root / "roots.txt"
            source.write_text(json.dumps(fixture()), encoding="utf-8")
            args = [
                "--input",
                str(source),
                "--image-start",
                "0x1000",
                "--image-end",
                "0x1100",
                "--json-out",
                str(receipt),
                "--roots-out",
                str(roots),
            ]
            self.assertEqual(normalizer.main(args), 0)
            self.assertEqual(
                roots.read_text(encoding="utf-8"), "0x00001000\n0x00001010\n"
            )
            self.assertEqual(normalizer.main(args), 2)

    def test_cli_rejects_duplicate_outputs_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.json"
            output = root / "duplicate.out"
            source.write_text(json.dumps(fixture()), encoding="utf-8")
            self.assertEqual(
                normalizer.main(
                    [
                        "--input",
                        str(source),
                        "--image-start",
                        "0x1000",
                        "--image-end",
                        "0x1100",
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
