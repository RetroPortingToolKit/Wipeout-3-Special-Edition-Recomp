from __future__ import annotations

import io
import json
import tempfile
import unittest
from contextlib import contextmanager, redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

from psx_catalog.canonical import (
    JSON_SAFE_INTEGER_LIMIT,
    CanonicalizationError,
    canonical_json_bytes,
    canonical_sha256,
    load_json_text,
    sha256_bytes,
    sha256_file,
)
from psx_catalog.cli import main
from psx_catalog.model import (
    ADDRESS_LIMIT,
    AddressRange,
    Catalog,
    DelaySlotOwnership,
    ExecutableVariant,
    MediaFileProvenance,
    PrecompiledArtifact,
    SourceMediaProvenance,
    ValidationError,
)
from psx_catalog.selection import CatalogSelectionError, CatalogSelector, SelectionRequest


EXECUTABLE_A = bytes(range(256))
EXECUTABLE_B = bytes(reversed(range(256)))
EXECUTABLE_C = bytes([0x5A]) * 256
HASH_A = sha256_bytes(EXECUTABLE_A)
HASH_B = sha256_bytes(EXECUTABLE_B)
HASH_C = sha256_bytes(EXECUTABLE_C)
HASH_MEDIA = sha256_bytes(b"synthetic-media")


def artifact_bytes(artifact_id: str) -> bytes:
    return f"native-artifact:{artifact_id}".encode("ascii")


def media() -> SourceMediaProvenance:
    return SourceMediaProvenance(
        source_id="fixture:disc-set-a",
        format="bin-cue",
        files=(
            MediaFileProvenance(
                role="data",
                name="track-01.bin",
                size=4096,
                sha256=HASH_MEDIA,
            ),
        ),
    )


def variant(
    executable: bytes,
    artifact_id: str,
    *,
    start: int = 0x1000,
    end: int = 0x1100,
    relocation_metadata: dict | None = None,
    roots: tuple[int, ...] | None = None,
    executed_ranges: tuple[AddressRange, ...] | None = None,
    delay_slots: tuple[DelaySlotOwnership, ...] = (),
    artifact_path: str | None = None,
) -> ExecutableVariant:
    if len(executable) != end - start:
        raise AssertionError("fixture executable length must equal its load range")
    region = AddressRange(start, end)
    native = artifact_bytes(artifact_id)
    return ExecutableVariant(
        load_range=region,
        sha256=sha256_bytes(executable),
        relocation_metadata=relocation_metadata,
        dispatch_roots=roots if roots is not None else (start,),
        executed_ranges=executed_ranges if executed_ranges is not None else (region,),
        delay_slot_owners=delay_slots,
        source_media=media(),
        artifact=PrecompiledArtifact(
            artifact_id,
            artifact_path or f"precompiled/{artifact_id}.bin",
            sha256_bytes(native),
        ),
    )


def catalog_with(*variants: ExecutableVariant) -> Catalog:
    return Catalog(
        catalog_id="synthetic-region-catalog",
        stack_window=AddressRange(0xF000, 0x10000),
        page_size=0x1000,
        variants=tuple(sorted(variants, key=lambda item: item.identity_sha256)),
    )


def request_for(item: ExecutableVariant) -> SelectionRequest:
    return SelectionRequest.from_dict(item.identity_dict())


def write_artifacts(catalog: Catalog, root: Path) -> None:
    for item in catalog.variants:
        target = root.joinpath(*item.artifact.path.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(artifact_bytes(item.artifact.id))


@contextmanager
def verified_selector(catalog: Catalog):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        write_artifacts(catalog, root)
        yield CatalogSelector(catalog, root), root


class CanonicalJsonTests(unittest.TestCase):
    def test_canonical_objects_and_hashes_are_stable(self) -> None:
        value = {"z": [3, 2, 1], "a": {"y": True, "x": 2}}
        self.assertEqual(canonical_json_bytes(value), b'{"a":{"x":2,"y":true},"z":[3,2,1]}')
        self.assertEqual(sha256_bytes(b"abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        self.assertEqual(
            canonical_sha256({"a": 2, "b": 1}),
            "d3626ac30a87e6f7a6428233b3c68299976865fa5508e4267c5415c76af7a772",
        )

    def test_duplicate_keys_floats_and_non_finite_numbers_are_rejected(self) -> None:
        for text in (
            '{"a":1,"a":2}',
            '{"outer":{"a":1,"a":2}}',
            '{"a":1.0}',
            '{"a":NaN}',
            '{"a":Infinity}',
        ):
            with self.subTest(text=text), self.assertRaises(CanonicalizationError):
                load_json_text(text)

    def test_non_interoperable_integers_and_surrogates_are_rejected(self) -> None:
        self.assertEqual(load_json_text(str(JSON_SAFE_INTEGER_LIMIT)), JSON_SAFE_INTEGER_LIMIT)
        with self.assertRaises(CanonicalizationError):
            load_json_text(str(JSON_SAFE_INTEGER_LIMIT + 1))
        with self.assertRaises(CanonicalizationError):
            canonical_json_bytes("\ud800")

    def test_manifest_round_trip_is_byte_identical(self) -> None:
        catalog = catalog_with(variant(EXECUTABLE_A, "region-a"))
        canonical = catalog.canonical_json()
        self.assertEqual(canonical, Catalog.from_dict(load_json_text(canonical)).canonical_json())
        self.assertEqual(canonical, catalog.canonical_json())
        self.assertTrue(canonical.endswith("\n"))
        self.assertNotIn(": ", canonical)
        self.assertEqual(
            catalog.sha256,
            "42c2277ab25a2f9c3483c0489c0df93ed9b04270ca36f8a29b51d00622615355",
        )
        self.assertEqual(catalog.sha256, canonical_sha256(load_json_text(canonical)))


class ValidationTests(unittest.TestCase):
    def test_32_bit_range_boundaries_and_types_are_enforced(self) -> None:
        self.assertEqual(AddressRange(ADDRESS_LIMIT - 4, ADDRESS_LIMIT).length, 4)
        for start, end in (
            (-4, 4),
            (0, 0),
            (4, 4),
            (ADDRESS_LIMIT, ADDRESS_LIMIT + 4),
            (0, ADDRESS_LIMIT + 1),
            (True, 4),
        ):
            with self.subTest(start=start, end=end), self.assertRaises(ValidationError):
                AddressRange(start, end)

    def test_alignment_and_delay_slot_overflow_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValidationError, "load_range"):
            variant(bytes(255), "unaligned", end=0x10FF)
        with self.assertRaisesRegex(ValidationError, "32-bit"):
            DelaySlotOwnership(ADDRESS_LIMIT - 4, ADDRESS_LIMIT, ADDRESS_LIMIT - 4)
        with self.assertRaisesRegex(ValidationError, "4-byte aligned"):
            DelaySlotOwnership(0x1002, 0x1006, 0x1000)

    def test_stack_overlap_is_rejected_at_catalog_boundary(self) -> None:
        overlapping = variant(bytes(256), "stack", start=0xF000, end=0xF100)
        with self.assertRaisesRegex(ValidationError, "stack_window"):
            catalog_with(overlapping)

    def test_overlapping_and_adjacent_executed_ranges_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValidationError, "must not overlap"):
            variant(
                EXECUTABLE_A,
                "overlap",
                executed_ranges=(AddressRange(0x1000, 0x1080), AddressRange(0x107C, 0x1100)),
            )
        with self.assertRaisesRegex(ValidationError, "coalesced"):
            variant(
                EXECUTABLE_A,
                "adjacent",
                executed_ranges=(AddressRange(0x1000, 0x1080), AddressRange(0x1080, 0x1100)),
            )

    def test_declared_data_gaps_cannot_be_dispatch_roots_or_delay_slots(self) -> None:
        ranges = (AddressRange(0x1000, 0x1040), AddressRange(0x1080, 0x1100))
        item = variant(EXECUTABLE_A, "data-gap", roots=(0x1000,), executed_ranges=ranges)
        self.assertEqual(item.data_ranges, (AddressRange(0x1040, 0x1080),))
        self.assertFalse(item.is_executable_address(0x1040))
        with self.assertRaisesRegex(ValidationError, "inside executed_ranges"):
            variant(EXECUTABLE_A, "data-root", roots=(0x1040,), executed_ranges=ranges)
        owner = DelaySlotOwnership(0x103C, 0x1040, 0x1000)
        with self.assertRaisesRegex(ValidationError, "share an executed range"):
            variant(
                EXECUTABLE_A,
                "data-slot",
                roots=(0x1000,),
                executed_ranges=ranges,
                delay_slots=(owner,),
            )

    def test_roots_must_be_sorted_unique_aligned_and_executable(self) -> None:
        with self.assertRaisesRegex(ValidationError, "roots must be sorted"):
            variant(EXECUTABLE_A, "unsorted", roots=(0x1080, 0x1000))
        with self.assertRaisesRegex(ValidationError, "duplicates"):
            variant(EXECUTABLE_A, "duplicate-root", roots=(0x1000, 0x1000))
        with self.assertRaisesRegex(ValidationError, "4-byte aligned"):
            variant(EXECUTABLE_A, "unaligned-root", roots=(0x1002,))
        with self.assertRaisesRegex(ValidationError, "inside executed_ranges"):
            variant(
                EXECUTABLE_A,
                "outside",
                roots=(0x1080,),
                executed_ranges=(AddressRange(0x1000, 0x1080),),
            )

    def test_portable_artifact_paths_cover_posix_windows_and_unicode(self) -> None:
        PrecompiledArtifact("unicode", "native/日本語.bin", sha256_bytes(b"x"))
        invalid = (
            "/absolute.bin",
            "C:/native.bin",
            "C:\\native.bin",
            "\\\\server\\share\\native.bin",
            "../native.bin",
            "native/../escape.bin",
            "native/./file.bin",
            "native//file.bin",
            "native/con.dll",
            "native/AUX.txt",
            "native/trailing.",
            "native/trailing ",
            "native/file?.bin",
            "native/e\u0301.bin",
            "native/\u202efile.bin",
        )
        for path in invalid:
            with self.subTest(path=path), self.assertRaises(ValidationError):
                PrecompiledArtifact("bad", path, sha256_bytes(b"x"))

    def test_portable_artifact_path_collisions_are_rejected(self) -> None:
        first = variant(EXECUTABLE_A, "first", artifact_path="native/Region.bin")
        second = variant(EXECUTABLE_B, "second", artifact_path="native/region.bin")
        with self.assertRaisesRegex(ValidationError, "portable paths"):
            catalog_with(first, second)

    def test_media_names_are_portable_base_names(self) -> None:
        for name in ("folder/file.bin", "..", ".hidden", "nul.bin", "e\u0301.bin"):
            with self.subTest(name=name), self.assertRaises(ValidationError):
                MediaFileProvenance("data", name, 1, HASH_MEDIA)

    def test_same_address_different_content_epochs_are_allowed(self) -> None:
        catalog = catalog_with(
            variant(EXECUTABLE_A, "epoch-a"),
            variant(EXECUTABLE_B, "epoch-b"),
        )
        self.assertEqual(len(catalog.variants), 2)

    def test_duplicate_executable_identity_is_rejected(self) -> None:
        known = variant(EXECUTABLE_A, "duplicate")
        with self.assertRaisesRegex(ValidationError, "duplicate executable identity"):
            Catalog(
                catalog_id="synthetic-region-catalog",
                stack_window=AddressRange(0xF000, 0x10000),
                page_size=0x1000,
                variants=(known, known),
            )

    def test_relocation_identity_is_snapshotted_against_mutation(self) -> None:
        metadata = {"base": 0x1000, "entries": [{"kind": "R_MIPS_32", "offset": 4}]}
        item = variant(EXECUTABLE_A, "immutable", relocation_metadata=metadata)
        original_identity = item.identity_sha256
        original_manifest = item.to_dict()
        metadata["base"] = 0x2000
        assert item.relocation_metadata is not None
        item.relocation_metadata["base"] = 0x3000
        self.assertEqual(item.identity_sha256, original_identity)
        self.assertEqual(item.to_dict(), original_manifest)


class DelaySlotTests(unittest.TestCase):
    def test_cross_page_delay_slot_belongs_to_the_declared_region(self) -> None:
        owner = DelaySlotOwnership(branch_pc=0x1FFC, delay_slot_pc=0x2000, owner_root=0x1000)
        executable = bytes(0x1004)
        item = variant(
            executable,
            "cross-page",
            start=0x1000,
            end=0x2004,
            delay_slots=(owner,),
        )
        self.assertTrue(owner.crosses_page(0x1000))
        self.assertEqual(item.delay_slot_owners[0].delay_slot_pc, 0x2000)

    def test_delay_slot_cannot_escape_its_executed_range(self) -> None:
        owner = DelaySlotOwnership(branch_pc=0x1FFC, delay_slot_pc=0x2000, owner_root=0x1000)
        with self.assertRaisesRegex(ValidationError, "share an executed range"):
            variant(
                bytes(0x1008),
                "cross-page-gap",
                start=0x1000,
                end=0x2008,
                executed_ranges=(AddressRange(0x1000, 0x2000), AddressRange(0x2004, 0x2008)),
                delay_slots=(owner,),
            )

    def test_page_size_must_be_valid(self) -> None:
        owner = DelaySlotOwnership(0x1000, 0x1004, 0x1000)
        for page_size in (0, 3, 6):
            with self.subTest(page_size=page_size), self.assertRaises(ValidationError):
                owner.crosses_page(page_size)


class SelectionTests(unittest.TestCase):
    def test_a_b_a_same_address_epoch_selection_is_exact_and_repeatable(self) -> None:
        epoch_a = variant(EXECUTABLE_A, "epoch-a")
        epoch_b = variant(EXECUTABLE_B, "epoch-b")
        catalog = catalog_with(epoch_a, epoch_b)
        with verified_selector(catalog) as (selector, _):
            selected = [
                selector.select(request_for(item), executable)
                for item, executable in (
                    (epoch_a, EXECUTABLE_A),
                    (epoch_b, EXECUTABLE_B),
                    (epoch_a, EXECUTABLE_A),
                )
            ]
        self.assertEqual([item.variant.artifact.id for item in selected], ["epoch-a", "epoch-b", "epoch-a"])
        self.assertEqual(selected[0].variant.identity_sha256, selected[2].variant.identity_sha256)
        self.assertEqual(selected[0].canonical_json(), selected[2].canonical_json())

    def test_unknown_code_fails_closed_with_a_compact_deterministic_receipt(self) -> None:
        known = variant(EXECUTABLE_A, "known")
        unknown = SelectionRequest.from_executable_bytes(0x1000, EXECUTABLE_C, None)
        with verified_selector(catalog_with(known)) as (selector, _):
            with self.assertRaises(CatalogSelectionError) as raised:
                selector.select(unknown, EXECUTABLE_C)
        receipt = raised.exception.receipt
        self.assertEqual(receipt.to_dict()["reason"], "unknown-executable-variant")
        self.assertEqual(receipt.canonical_json(), receipt.canonical_json())
        self.assertNotIn("timestamp", receipt.canonical_json())
        self.assertNotIn("artifact", receipt.canonical_json())

    def test_caller_digest_cannot_substitute_for_measured_executable_bytes(self) -> None:
        known = variant(EXECUTABLE_A, "known")
        with verified_selector(catalog_with(known)) as (selector, _):
            with self.assertRaises(CatalogSelectionError) as raised:
                selector.select(request_for(known), EXECUTABLE_B)
            self.assertEqual(raised.exception.receipt.reason, "request-executable-hash-mismatch")
            with self.assertRaises(CatalogSelectionError) as raised:
                selector.select(request_for(known), EXECUTABLE_A[:-4])
            self.assertEqual(raised.exception.receipt.reason, "request-executable-size-mismatch")

    def test_range_and_relocation_near_matches_are_rejected(self) -> None:
        relocated = variant(
            EXECUTABLE_A,
            "relocated",
            relocation_metadata={"base": 0x1000, "entries": [{"offset": 4, "kind": "R_MIPS_32"}]},
        )
        with verified_selector(catalog_with(relocated)) as (selector, _):
            wrong_range = SelectionRequest.from_executable_bytes(0x2000, EXECUTABLE_A, relocated.relocation_metadata)
            with self.assertRaises(CatalogSelectionError):
                selector.select(wrong_range, EXECUTABLE_A)
            no_relocation = SelectionRequest.from_executable_bytes(0x1000, EXECUTABLE_A, None)
            with self.assertRaises(CatalogSelectionError):
                selector.select(no_relocation, EXECUTABLE_A)

    def test_every_single_byte_mutation_is_rejected(self) -> None:
        known = variant(EXECUTABLE_A, "known")
        catalog = catalog_with(known)
        with verified_selector(catalog) as (selector, _):
            for offset in range(len(EXECUTABLE_A)):
                mutated = bytearray(EXECUTABLE_A)
                mutated[offset] ^= 0x01
                with self.subTest(offset=offset, mode="claimed"), self.assertRaises(CatalogSelectionError):
                    selector.select(request_for(known), mutated)
                measured_request = SelectionRequest.from_executable_bytes(0x1000, mutated, None)
                with self.subTest(offset=offset, mode="measured"), self.assertRaises(CatalogSelectionError):
                    selector.select(measured_request, mutated)

    def test_stack_window_request_is_rejected(self) -> None:
        known = variant(EXECUTABLE_A, "known")
        request = SelectionRequest.from_executable_bytes(0xF000, EXECUTABLE_A, None)
        with verified_selector(catalog_with(known)) as (selector, _):
            with self.assertRaisesRegex(CatalogSelectionError, "stack-window"):
                selector.select(request, EXECUTABLE_A)

    def test_native_artifact_is_hashed_before_selection_succeeds(self) -> None:
        known = variant(EXECUTABLE_A, "known")
        catalog = catalog_with(known)
        with verified_selector(catalog) as (selector, root):
            result = selector.select(request_for(known), EXECUTABLE_A)
            self.assertEqual(result.artifact_size, len(artifact_bytes("known")))
            self.assertEqual(
                result.to_dict()["binding"],
                {
                    "load_range": known.load_range.to_dict(),
                    "dispatch_roots": list(known.dispatch_roots),
                    "executed_ranges": [item.to_dict() for item in known.executed_ranges],
                },
            )
            artifact = root.joinpath(*known.artifact.path.split("/"))
            artifact.write_bytes(b"tampered-native-code")
            with self.assertRaises(CatalogSelectionError) as raised:
                selector.select(request_for(known), EXECUTABLE_A)
            self.assertEqual(raised.exception.receipt.reason, "native-artifact-integrity-failed")

    def test_missing_or_indirect_native_artifact_is_rejected(self) -> None:
        known = variant(EXECUTABLE_A, "known")
        catalog = catalog_with(known)
        with verified_selector(catalog) as (selector, root):
            artifact = root.joinpath(*known.artifact.path.split("/"))
            artifact.unlink()
            with self.assertRaises(CatalogSelectionError) as raised:
                selector.select(request_for(known), EXECUTABLE_A)
            self.assertEqual(raised.exception.receipt.reason, "native-artifact-unavailable")

            target = root / "target.bin"
            target.write_bytes(artifact_bytes("known"))
            artifact.write_bytes(artifact_bytes("known"))
            with patch.object(CatalogSelector, "_is_link_or_junction", return_value=True):
                with self.assertRaises(CatalogSelectionError) as raised:
                    selector.select(request_for(known), EXECUTABLE_A)
            self.assertEqual(raised.exception.receipt.reason, "native-artifact-path-is-indirect")

            artifact.unlink()
            try:
                artifact.symlink_to(target)
            except OSError:
                return
            with self.assertRaises(CatalogSelectionError) as raised:
                selector.select(request_for(known), EXECUTABLE_A)
            self.assertEqual(raised.exception.receipt.reason, "native-artifact-path-is-indirect")

    def test_streaming_file_selection_hashes_executable_bytes(self) -> None:
        known = variant(EXECUTABLE_A, "known")
        with verified_selector(catalog_with(known)) as (selector, root):
            executable = root / "input.exe"
            executable.write_bytes(EXECUTABLE_A)
            result = selector.select_file(request_for(known), executable)
            self.assertEqual(result.variant.identity_sha256, known.identity_sha256)
            executable.write_bytes(EXECUTABLE_B)
            with self.assertRaises(CatalogSelectionError):
                selector.select_file(request_for(known), executable)


class HashAndCliTests(unittest.TestCase):
    def test_file_hash_is_chunked_and_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bytes.bin"
            path.write_bytes(b"catalog-test" * 100)
            first = sha256_file(path, chunk_size=3)
            second = sha256_file(path, chunk_size=4096)
            self.assertEqual(first, second)

    def test_cli_status_and_exit_contracts(self) -> None:
        known = variant(EXECUTABLE_A, "known")
        catalog = catalog_with(known)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / "manifest.json"
            request = root / "request.json"
            executable = root / "executable.bin"
            payload = root / "payload.bin"
            artifacts = root / "artifacts"
            manifest.write_text(catalog.canonical_json(), encoding="utf-8")
            request.write_text(
                canonical_json_bytes(request_for(known).to_dict()).decode("utf-8"),
                encoding="utf-8",
            )
            executable.write_bytes(EXECUTABLE_A)
            payload.write_bytes(b"abc")
            write_artifacts(catalog, artifacts)

            cases = (
                (["validate", str(manifest)], 0, "valid"),
                (["select", str(manifest), str(request), str(executable), str(artifacts)], 0, "selected"),
                (["hash", str(payload)], 0, None),
            )
            for arguments, expected_code, expected_status in cases:
                output = io.StringIO()
                with self.subTest(arguments=arguments), redirect_stdout(output):
                    self.assertEqual(main(arguments), expected_code)
                parsed = json.loads(output.getvalue())
                if expected_status is not None:
                    self.assertEqual(parsed["status"], expected_status)

            unknown = SelectionRequest.from_executable_bytes(0x1000, EXECUTABLE_C, None)
            unknown_path = root / "unknown.json"
            unknown_executable = root / "unknown.bin"
            unknown_path.write_text(
                canonical_json_bytes(unknown.to_dict()).decode("utf-8"),
                encoding="utf-8",
            )
            unknown_executable.write_bytes(EXECUTABLE_C)
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(
                    main(["select", str(manifest), str(unknown_path), str(unknown_executable), str(artifacts)]),
                    3,
                )
            self.assertEqual(json.loads(output.getvalue())["status"], "rejected")

            malformed = root / "malformed.json"
            malformed.write_text('{"schema":"a","schema":"b"}', encoding="utf-8")
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(main(["validate", str(malformed)]), 2)
            self.assertEqual(json.loads(output.getvalue())["status"], "invalid")

            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(main(["select"]), 2)
            self.assertEqual(json.loads(output.getvalue())["status"], "invalid")


if __name__ == "__main__":
    unittest.main()
