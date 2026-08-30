import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ArtifactHygieneTests(unittest.TestCase):
    def test_diagnostic_sink_and_root_streams_are_ignored(self) -> None:
        for relative in (
            "artifacts/probe.bin",
            "runtime-diagnostic.err",
            "runtime-diagnostic.out",
        ):
            result = subprocess.run(
                [
                    "git",
                    "-C",
                    str(ROOT),
                    "check-ignore",
                    "--quiet",
                    "--no-index",
                    relative,
                ],
                check=False,
            )
            self.assertEqual(result.returncode, 0, relative)


if __name__ == "__main__":
    unittest.main()
