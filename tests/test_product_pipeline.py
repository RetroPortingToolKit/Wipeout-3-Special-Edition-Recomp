from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import Any, cast

from tools.product import pipeline


class ProductPipelineTests(unittest.TestCase):
    def test_pinned_profile_is_strict_and_contains_only_known_payloads(self) -> None:
        profile, profile_sha = pipeline._load_profile(pipeline.DEFAULT_PROFILE)

        self.assertEqual(profile["schema"], pipeline.PROFILE_SCHEMA)
        self.assertEqual(len(profile["runtime_variants"]), 1)
        self.assertEqual(profile["runtime_variants"][0]["id"], "nfe-entry-5")
        self.assertEqual(len(profile_sha), 64)
        with self.assertRaises(pipeline.UnknownVariantError):
            pipeline._validate_regions(
                {
                    "schema": pipeline.REGIONS_SCHEMA,
                    "catalog_id": profile["catalog"]["id"],
                    "page_size": profile["catalog"]["page_size"],
                    "stack_window": profile["catalog"]["stack_window"],
                    "variants": [
                        {
                            "id": "unlisted-payload",
                            "compile": True,
                            "entry_points": [4096],
                            "code_ranges": [{"start": 4096, "end_exclusive": 4100}],
                            "data_ranges": [],
                            "external_targets": [],
                            "indirect_target_sets": [],
                            "delay_slot_owners": [],
                            "maximum_instructions_per_shard": 64,
                        }
                    ],
                },
                profile,
            )

    def test_region_manifest_normalizes_deterministically(self) -> None:
        profile, _ = pipeline._load_profile(pipeline.DEFAULT_PROFILE)
        image = profile["main_executable"]["load_range"]
        document = {
            "schema": pipeline.REGIONS_SCHEMA,
            "catalog_id": profile["catalog"]["id"],
            "page_size": profile["catalog"]["page_size"],
            "stack_window": profile["catalog"]["stack_window"],
            "variants": [
                {
                    "id": "main-exe",
                    "compile": True,
                    "entry_points": [profile["main_executable"]["entry_pc"]],
                    "code_ranges": [image],
                    "data_ranges": [],
                    "external_targets": [],
                    "indirect_target_sets": [],
                    "delay_slot_owners": [],
                    "maximum_instructions_per_shard": 4096,
                }
            ],
        }
        first = pipeline._validate_regions(document, profile)
        second = pipeline._validate_regions(json.loads(json.dumps(document)), profile)
        self.assertEqual(
            pipeline.canonical_json_bytes(first), pipeline.canonical_json_bytes(second)
        )
        invalid_entry = json.loads(json.dumps(document))
        invalid_entry["variants"][0]["entry_points"] = [image["start"]]
        with self.assertRaises(pipeline.ProductError):
            pipeline._validate_regions(invalid_entry, profile)

    def test_compiler_manifest_is_stable_and_has_no_runtime_fallback(self) -> None:
        record = {
            "id": "main-exe",
            "image_kind": "ps-x-exe",
            "image_sha256": "a" * 64,
            "sha256": "b" * 64,
            "load_range": {"start": 4096, "end_exclusive": 4112},
            "relocation_metadata": None,
        }
        region = {
            "entry_points": [4096],
            "code_ranges": [{"start": 4096, "end_exclusive": 4112}],
            "data_ranges": [],
            "external_targets": [],
            "indirect_target_sets": [],
            "maximum_instructions_per_shard": 64,
        }
        first, identity = pipeline._compiler_manifest(
            record, region, catalog_sha256="c" * 64
        )
        second, identity_again = pipeline._compiler_manifest(
            record, region, catalog_sha256="c" * 64
        )
        self.assertEqual(first, second)
        self.assertEqual(identity, identity_again)
        self.assertIn("schema=clean-aot-compiler-manifest-v1", first)
        self.assertNotIn("interpreter", first.lower())
        self.assertNotIn("jit", first.lower())

    def test_raw_payload_requires_explicit_image_compiler_protocol(self) -> None:
        record = {
            "image_kind": "raw-payload",
            "image_path": Path("payload.bin"),
        }
        compiler = Path("clean_aot_compiler_cli.exe")
        with self.assertRaises(pipeline.ProductError):
            pipeline._compiler_command(
                compiler, "clean-aot-v1", record, Path("m"), Path("o")
            )
        command = pipeline._compiler_command(
            compiler,
            "clean-aot-image-v1",
            record,
            Path("m"),
            Path("o"),
        )
        self.assertIn("--image-kind", command)
        self.assertIn("raw-payload", command)

    def test_catalog_is_validated_through_existing_catalog_cli(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            artifact = work / "native.a"
            artifact.write_bytes(b"synthetic native artifact")
            artifact_hash, _ = pipeline.sha256_file(artifact)
            source_hash = "1" * 64
            source_media = {
                "source_id": "synthetic-source",
                "format": "synthetic-bin-cue",
                "files": [
                    {
                        "role": "bios",
                        "name": "SCPH1001.BIN",
                        "size": 1,
                        "sha256": "2" * 64,
                    }
                ],
            }
            catalog_input = {
                "schema": pipeline.CATALOG_INPUT_SCHEMA,
                "catalog_id": "synthetic-product",
                "address_bits": 32,
                "page_size": 4096,
                "stack_window": {"start": 36864, "end_exclusive": 40960},
                "variants": [
                    {
                        "id": "main-exe",
                        "kind": "ps-x-exe",
                        "image_kind": "ps-x-exe",
                        "identity": {
                            "load_range": {"start": 4096, "end_exclusive": 4112},
                            "sha256": source_hash,
                            "relocation_metadata": None,
                        },
                        "image_sha256": "3" * 64,
                        "dispatch_roots": [4096],
                        "executed_ranges": [{"start": 4096, "end_exclusive": 4112}],
                        "delay_slot_owners": [],
                        "source_media": source_media,
                        "compile": True,
                        "artifact": None,
                    }
                ],
            }
            artifacts = {
                "main-exe": {
                    "id": "synthetic-native",
                    "path": "native.a",
                    "sha256": artifact_hash,
                    "size": artifact.stat().st_size,
                }
            }
            catalog_variants = cast(list[dict[str, Any]], catalog_input["variants"])
            document = pipeline._catalog_document(
                catalog_input,
                {"main-exe": catalog_variants[0]},
                artifacts,
            )
            digest, receipt = pipeline._validate_catalog_with_cli(
                document,
                work,
                max_output_bytes=1048576,
            )
            self.assertEqual(len(digest), 64)
            self.assertEqual(receipt["returncode"], 0)
            self.assertEqual(document["schema"], "psxrecomp.clean-aot.catalog.v1")
            with self.assertRaises(pipeline.ProductError):
                pipeline._catalog_document(
                    catalog_input,
                    {"main-exe": catalog_variants[0]},
                    {},
                )

    def test_output_scanner_rejects_input_bytes_and_unsafe_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            generated = root / "generated"
            generated.mkdir()
            (generated / "main.cpp").write_text(
                "int clean_product() { return 1; }\n", encoding="utf-8"
            )
            digest, _ = pipeline.sha256_file(generated / "main.cpp")
            limits = {
                "max_generated_files": 8,
                "max_generated_bytes": 1024,
                "max_generated_file_bytes": 1024,
            }
            files = pipeline._scan_output_tree(
                generated,
                limits=limits,
                forbidden_hashes=set(),
                forbidden_names={"sces_028.45"},
            )
            self.assertEqual(files[0]["sha256"], digest)
            with self.assertRaises(pipeline.ProductError):
                pipeline._safe_relative_path("../escape.cpp", "test")
            with self.assertRaises(pipeline.ProductError):
                pipeline._safe_relative_path("cmake;$escape.cpp", "test")
            with self.assertRaises(pipeline.ProductError):
                pipeline._assert_external_output(pipeline.ROOT / "product-output")
            with self.assertRaises(pipeline.ProductError):
                pipeline._scan_output_tree(
                    generated,
                    limits=limits,
                    forbidden_hashes={digest},
                    forbidden_names=set(),
                )

    def test_dry_run_is_deterministic_and_does_not_create_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cue = root / "game.cue"
            track = root / "Track 01.bin"
            bios = root / "SCPH1001.BIN"
            patch = root / "pal_track01.xdelta3"
            xdelta3 = root / "xdelta3.exe"
            compiler = root / "clean_aot_compiler_cli.exe"
            track.write_bytes(b"synthetic track")
            cue.write_text(
                'FILE "Track 01.bin" BINARY\nTRACK 01 MODE2/2352\n', encoding="utf-8"
            )
            bios.write_bytes(b"bios")
            patch.write_bytes(b"patch")
            xdelta3.write_bytes(b"tool")
            compiler.write_bytes(b"compiler")
            output = root / "external-output"
            first = pipeline.run_dry_run(
                cue=cue,
                bios=bios,
                patch=patch,
                xdelta3=xdelta3,
                compiler=compiler,
                output=output,
            )
            second = pipeline.run_dry_run(
                cue=cue,
                bios=bios,
                patch=patch,
                xdelta3=xdelta3,
                compiler=compiler,
                output=output,
            )
            self.assertEqual(
                pipeline.canonical_json_bytes(first),
                pipeline.canonical_json_bytes(second),
            )
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
