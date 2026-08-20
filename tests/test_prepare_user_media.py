from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROVENANCE = ROOT / "tools" / "provenance"
sys.path.insert(0, str(PROVENANCE))
MODULE_PATH = PROVENANCE / "prepare_user_media.py"
SPEC = importlib.util.spec_from_file_location("prepare_user_media", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
prepare_user_media = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prepare_user_media)


def identity(payload: bytes) -> dict[str, object]:
    import hashlib
    import zlib

    return {
        "size": len(payload),
        "md5": hashlib.md5(payload).hexdigest(),
        "sha1": hashlib.sha1(payload).hexdigest(),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "crc32": f"{zlib.crc32(payload) & 0xFFFFFFFF:08x}",
    }


class PrepareUserMediaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.track1 = b"already-patched"
        self.track2 = b"audio"
        (self.root / "track01.bin").write_bytes(self.track1)
        (self.root / "track02.bin").write_bytes(self.track2)
        self.cue = self.root / "input.cue"
        self.cue.write_text(
            'FILE "track01.bin" BINARY\n'
            '  TRACK 01 MODE2/2352\n'
            '    INDEX 01 00:00:00\n'
            'FILE "track02.bin" BINARY\n'
            '  TRACK 02 AUDIO\n'
            '    INDEX 00 00:00:00\n'
            '    INDEX 01 00:02:00\n',
            encoding="utf-8",
        )
        manifest = {
            "game": {"id": "TEST", "cue_tracks": 2},
            "track_01": {
                "redump": identity(b"redump"),
                "hueponik_pal": identity(self.track1),
            },
            "audio_tracks": [{"track": 2, **identity(self.track2)}],
            "hueponik_patch": {"size": 0, "sha256": ""},
            "bios": {"scph1001": identity(b"bios")},
        }
        self.manifest = self.root / "manifest.json"
        self.manifest.write_text(json.dumps(manifest), encoding="utf-8")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def args(self, output: Path):
        return prepare_user_media.build_parser().parse_args(
            [
                "--cue",
                str(self.cue),
                "--output",
                str(output),
                "--manifest",
                str(self.manifest),
            ]
        )

    def test_stages_and_reverifies_patched_media(self) -> None:
        output = self.root / "prepared"
        report = prepare_user_media.stage(self.args(output))
        self.assertEqual(report["track_01"]["identity"], "hueponik_pal")
        self.assertEqual((output / "track01.bin").read_bytes(), self.track1)
        self.assertIn('FILE "track02.bin" BINARY', (output / self.cue.name).read_text())

    def test_refuses_existing_output_directory(self) -> None:
        output = self.root / "prepared"
        output.mkdir()
        with self.assertRaises(prepare_user_media.verify_media.VerificationError):
            prepare_user_media.stage(self.args(output))

    def test_redump_requires_patch_and_xdelta(self) -> None:
        (self.root / "track01.bin").write_bytes(b"redump")
        with self.assertRaises(prepare_user_media.verify_media.VerificationError):
            prepare_user_media.stage(self.args(self.root / "prepared"))


if __name__ == "__main__":
    unittest.main()
