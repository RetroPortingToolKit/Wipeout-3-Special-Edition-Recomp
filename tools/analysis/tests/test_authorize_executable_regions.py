import hashlib
import struct
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.analysis.authorize_executable_regions import (
    analyze,
    corridor_evidence,
    prove_direct_corridors,
    resolve_indirect,
)
from tools.analysis.generate_finite_indirect_manifest import build_manifest


BASE = 0x80160000
PC = 0x8016FC74


def write_word(image: bytearray, pc: int, word: int) -> None:
    struct.pack_into("<I", image, pc - BASE, word)


def computed_jump_image() -> bytes:
    image = bytearray(0x10000)
    words = (
        0x3C028016,  # lui v0, 0x8016
        0x3442FC7C,  # ori v0, v0, 0xfc7c
        0x00051A42,  # srl v1, a1, 9
        0x30630070,  # andi v1, v1, 0x70
        0x00621020,  # add v0, v1, v0
        0x00400008,  # jr v0
        0x00053282,  # srl a2, a1, 10 (delay slot)
    )
    for index, word in enumerate(words):
        write_word(image, PC - 20 + index * 4, word)
    for target in range(0x8016FC7C, 0x8016FCEC + 1, 0x10):
        write_word(image, target, 0)
    return bytes(image)


def zero_trip_loop_image() -> tuple[bytes, int]:
    image = bytearray(0x10000)
    pc = BASE + 0x2564
    words = (
        0x3C110000,  # lui s1, 0
        0x26310000,  # addiu s1, s1, 0
        0x12200007,  # beq s1, zero, continuation
        0x00000000,
        0x8E080000,  # lw t0, 0(s0)
        0x26100004,  # addiu s0, s0, 4
        0x0100F809,  # jalr t0
        0x2631FFFF,  # addiu s1, s1, -1
        0x1620FFFB,  # bne s1, zero, loop
        0x00000000,
        0x00000000,
    )
    for index, word in enumerate(words):
        write_word(image, pc - 24 + index * 4, word)
    return bytes(image), pc


class FiniteIndirectTests(unittest.TestCase):
    def test_direct_corridor_stops_at_unresolved_indirect(self) -> None:
        image = bytearray(32)
        write_word(image, BASE, 0x24020001)       # addiu v0,zero,1
        write_word(image, BASE + 4, 0x00400008)   # jr v0
        write_word(image, BASE + 8, 0)            # delay slot
        write_word(image, BASE + 12, 0x24030002)  # must remain unknown
        self.assertEqual(
            prove_direct_corridors(bytes(image), BASE, {BASE}),
            {BASE, BASE + 4, BASE + 8},
        )

    def test_direct_corridor_rejects_ambiguous_epoch_range(self) -> None:
        image = bytes(32)
        self.assertEqual(
            prove_direct_corridors(image, BASE, {BASE}, (BASE + 8, BASE + 16)),
            {BASE, BASE + 4},
        )

    def test_direct_corridor_receipt_hashes_exact_recovered_bytes(self) -> None:
        image = bytes(range(32))
        receipt = corridor_evidence(image, BASE, {BASE + 4, BASE + 8}, {BASE + 4})
        self.assertEqual(receipt["recovered_instruction_count"], 2)
        self.assertEqual(receipt["recovered_ranges"][0]["start"], BASE + 4)
        self.assertEqual(
            receipt["recovered_ranges"][0]["sha256"],
            hashlib.sha256(image[4:12]).hexdigest(),
        )

    def test_authenticated_zero_trip_loop_proves_empty_target_set(self) -> None:
        image, pc = zero_trip_loop_image()
        self.assertEqual(
            resolve_indirect(image, BASE, pc, 8),
            ([], "authenticated_zero_trip_loop_unreachable"),
        )
        manifest = build_manifest(
            image, hashlib.sha256(image).hexdigest(), BASE,
            "synthetic-zero-trip", [(pc, 8)],
        )
        self.assertEqual(manifest["sites"][0]["targets"], [])

    def test_zero_trip_near_matches_fail_closed(self) -> None:
        image, pc = zero_trip_loop_image()
        for address, word in (
            (pc - 20, 0x26310001),  # non-zero count
            (pc - 16, 0x12200006),  # branch does not skip the loop
            (pc + 8, 0x1620FFFA),   # loop backedge differs
        ):
            changed = bytearray(image)
            write_word(changed, address, word)
            self.assertIsNone(resolve_indirect(bytes(changed), BASE, pc, 8))

    def test_authenticated_mask_proves_exact_finite_targets(self) -> None:
        image = computed_jump_image()
        resolution = resolve_indirect(image, BASE, PC, 2)
        self.assertEqual(
            resolution,
            ([0x8016FC7C + offset for offset in range(0, 0x80, 0x10)],
             "authenticated_masked_offset_construction"),
        )

    def test_near_match_and_missing_target_evidence_fail_closed(self) -> None:
        image = bytearray(computed_jump_image())
        write_word(image, PC - 4, 0x00621021)  # ADDU, not the proved ADD shape.
        self.assertIsNone(resolve_indirect(bytes(image), BASE, PC, 2))

        image = computed_jump_image()
        allowed = set(range(PC - 20, PC + 8, 4))
        report = analyze(image, BASE, {PC - 20}, allowed, "synthetic")
        self.assertEqual(report["indirect_targets"], [])
        self.assertEqual(report["unresolved_indirects"][0]["pc"], PC)

    def test_manifest_is_identity_bound_and_deterministic(self) -> None:
        image = computed_jump_image()
        digest = hashlib.sha256(image).hexdigest()
        first = build_manifest(image, digest, BASE, "synthetic", [(PC, 2)])
        second = build_manifest(image, digest, BASE, "synthetic", [(PC, 2)])
        self.assertEqual(
            json.dumps(first, indent=2, sort_keys=True) + "\n",
            json.dumps(second, indent=2, sort_keys=True) + "\n",
        )
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            build_manifest(image, "0" * 64, BASE, "synthetic", [(PC, 2)])

    def test_manifest_cli_round_trip_and_wrong_identity_rejection(self) -> None:
        image = computed_jump_image()
        digest = hashlib.sha256(image).hexdigest()
        script = Path(__file__).parents[1] / "generate_finite_indirect_manifest.py"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image_path = root / "image.bin"
            first = root / "first.json"
            second = root / "second.json"
            image_path.write_bytes(image)
            common = [
                sys.executable, str(script), "--image", str(image_path),
                "--image-sha256", digest, "--base", hex(BASE),
                "--name", "synthetic", "--site", f"{hex(PC)}:2",
            ]
            subprocess.run(common + ["--output", str(first)], check=True)
            subprocess.run(common + ["--output", str(second)], check=True)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            rejected = subprocess.run(
                common[:5] + ["0" * 64] + common[6:] +
                ["--output", str(root / "rejected.json")],
                capture_output=True,
            )
            self.assertEqual(rejected.returncode, 2)


if __name__ == "__main__":
    unittest.main()
