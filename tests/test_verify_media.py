from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "provenance" / "verify_media.py"
SPEC = importlib.util.spec_from_file_location("verify_media", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
verify_media = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verify_media)


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


class VerifyMediaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.track1 = b"redump-track"
        self.track2 = b"audio-track"
        (self.root / "track01.bin").write_bytes(self.track1)
        (self.root / "track02.bin").write_bytes(self.track2)
        (self.root / "disc.cue").write_text(
            'FILE "track01.bin" BINARY\n'
            '  TRACK 01 MODE2/2352\n'
            '    INDEX 01 00:00:00\n'
            'FILE "track02.bin" BINARY\n'
            '  TRACK 02 AUDIO\n'
            '    INDEX 01 00:00:00\n',
            encoding="utf-8",
        )
        manifest = {
            "game": {"id": "TEST", "cue_tracks": 2},
            "track_01": {"redump": identity(self.track1), "hueponik_pal": identity(b"patched")},
            "audio_tracks": [{"track": 2, **identity(self.track2)}],
            "hueponik_patch": {"size": 0, "sha256": ""},
            "bios": {"scph1001": identity(b"bios")},
        }
        self.manifest = self.root / "manifest.json"
        self.manifest.write_text(json.dumps(manifest), encoding="utf-8")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def args(self):
        return verify_media.build_parser().parse_args(
            ["--cue", str(self.root / "disc.cue"), "--manifest", str(self.manifest)]
        )

    def test_accepts_exact_cue_and_tracks(self) -> None:
        report = verify_media.verify(self.args())
        self.assertEqual(report["status"], "ok")
        self.assertEqual(report["track_01"]["identity"], "redump")
        self.assertEqual(report["cue"]["tracks"], 2)

    def test_rejects_mutated_audio(self) -> None:
        (self.root / "track02.bin").write_bytes(b"mutated")
        with self.assertRaises(verify_media.VerificationError):
            verify_media.verify(self.args())

    def test_rejects_wrong_track_mode(self) -> None:
        cue = self.root / "disc.cue"
        cue.write_text(cue.read_text(encoding="utf-8").replace("TRACK 02 AUDIO", "TRACK 02 MODE2/2352"))
        with self.assertRaises(verify_media.VerificationError):
            verify_media.verify(self.args())


if __name__ == "__main__":
    unittest.main()
