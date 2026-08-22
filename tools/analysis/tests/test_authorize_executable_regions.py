import struct
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.analysis.authorize_executable_regions import analyze, resolve_indirect
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


class FiniteIndirectTests(unittest.TestCase):
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
        import hashlib
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
        import hashlib
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
