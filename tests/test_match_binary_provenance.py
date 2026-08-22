from __future__ import annotations

import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "analysis" / "match_binary_provenance.py"
SPEC = importlib.util.spec_from_file_location("match_binary_provenance", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
match_binary_provenance = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = match_binary_provenance
SPEC.loader.exec_module(match_binary_provenance)


def blob(label: str, payload: bytes) -> object:
    return match_binary_provenance.Blob(
        label, payload, match_binary_provenance._sha256(payload)
    )


class BinaryProvenanceMatchTests(unittest.TestCase):
    def test_whole_and_partial_matches_are_maximal_and_deduplicated(self) -> None:
        query_a = bytes(range(96))
        query_b = b"prefix-" + bytes(range(80)) + b"-suffix"
        source_a = b"noise" + query_a + b"tail"
        source_b = b"left" + query_b[7:-7] + b"right"
        report = match_binary_provenance.match_provenance(
            [blob("a", query_a), blob("b", query_b)],
            [blob("source-a", source_a), blob("source-b", source_b)],
            anchor_bytes=8,
            min_match_bytes=16,
        )
        self.assertEqual(report["queries"][0]["status"], "whole-source-match")
        self.assertEqual(report["queries"][0]["matched_byte_union"], len(query_a))
        self.assertEqual(report["queries"][1]["status"], "partial-source-match")
        self.assertEqual(report["queries"][1]["longest_match"], 80)
        a_matches = [
            item
            for item in report["matches"]
            if item["query"] == "a" and item["source"] == "source-a"
        ]
        self.assertEqual(len(a_matches), 1)
        self.assertTrue(a_matches[0]["whole_query"])

    def test_repetitive_anchors_are_suppressed_without_false_claim(self) -> None:
        repeated = b"A" * 512
        report = match_binary_provenance.match_provenance(
            [blob("query", repeated)],
            [blob("source", repeated)],
            anchor_bytes=8,
            min_match_bytes=16,
        )
        self.assertGreater(
            report["parameters"]["suppressed_repetitive_anchor_count"], 0
        )
        self.assertEqual(report["queries"][0]["status"], "unmatched")

    def test_invalid_bounds_and_digest_fail_closed(self) -> None:
        valid = blob("valid", b"0123456789abcdef")
        invalid = match_binary_provenance.Blob("invalid", b"payload", "0" * 64)
        with self.assertRaises(match_binary_provenance.ProvenanceMatchError):
            match_binary_provenance.match_provenance([invalid], [valid])
        with self.assertRaises(match_binary_provenance.ProvenanceMatchError):
            match_binary_provenance.match_provenance(
                [valid], [valid], anchor_bytes=4, min_match_bytes=8
            )
        with self.assertRaises(match_binary_provenance.ProvenanceMatchError):
            match_binary_provenance.match_provenance(
                [valid], [valid], anchor_bytes=8, min_match_bytes=4
            )

    def test_cli_directory_filter_stable_labels_and_overwrite_refusal(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            queries = root / "queries"
            sources = root / "sources"
            queries.mkdir()
            sources.mkdir()
            payload = bytes(range(64))
            (queries / "region.bin").write_bytes(payload)
            (queries / "ignored.txt").write_text("ignored")
            (sources / "image.bin").write_bytes(b"head" + payload + b"tail")
            output = root / "receipt.json"
            argv = [
                "--query",
                str(queries),
                "--source",
                str(sources),
                "--include-suffix",
                ".bin",
                "--root",
                str(root),
                "--anchor-bytes",
                "8",
                "--minimum-match-bytes",
                "16",
                "--json-out",
                str(output),
            ]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(match_binary_provenance.main(argv), 0)
            receipt = json.loads(output.read_text())
            self.assertEqual(receipt["queries"][0]["label"], "queries/region.bin")
            self.assertEqual(receipt["queries"][0]["status"], "whole-source-match")
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(match_binary_provenance.main(argv), 2)


if __name__ == "__main__":
    unittest.main()
