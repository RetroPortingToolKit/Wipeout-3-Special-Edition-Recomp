import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class CompilerCliIdentityTests(unittest.TestCase):
    def test_identity_bound_emission_and_sparse_unknown_pc(self):
        cli = Path(sys.argv[1])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "image.bin"
            manifest = root / "image.manifest"
            output = root / "generated"
            payload = bytes.fromhex("01000124") + bytes.fromhex("00000000")
            digest = hashlib.sha256(payload).hexdigest()
            image.write_bytes(payload)
            manifest.write_text("\n".join((
                "schema=clean-aot-compiler-manifest-v1",
                "image_kind=raw-payload",
                f"variant_sha256={digest}",
                f"image_sha256={digest}",
                "load_start=0x1000",
                "load_end_exclusive=0x1008",
                "entry=0x1000",
                "code=0x1000-0x1004",
                "",
            )))
            subprocess.run((cli, "--image", image, "--manifest", manifest,
                            "--output", output), check=True, capture_output=True)
            table = (output / "entry_table.cpp").read_text()
            self.assertIn(digest, table)
            self.assertIn("{0x1000U,", table)
            self.assertNotIn("{0x1004U,", table)

            changed = root / "changed.bin"
            changed.write_bytes(payload[:-1] + b"\x01")
            rejected = subprocess.run(
                (cli, "--image", changed, "--manifest", manifest,
                 "--output", root / "rejected"), capture_output=True, text=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn("image SHA-256 mismatch", rejected.stderr)


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1])
