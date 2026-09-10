import hashlib
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HANDOFF = ROOT / "docs/HANDOFF.md"
ARCHIVE = ROOT / "docs/archive/HANDOFF-through-2026-08-28.md"
ARCHIVE_SHA1 = "ca54b925e90bc3271a1cfc87b84a3bdbe0800aa8"
REQUIRED_HEADINGS = (
    "Current verified state",
    "Next concrete step",
    "Open blockers",
    "Protected decisions",
    "Evidence map",
    "Ruled out",
)


class DocsContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.text = HANDOFF.read_text(encoding="utf-8")

    def test_handoff_is_a_bounded_current_dashboard(self) -> None:
        self.assertLessEqual(len(self.text.splitlines()), 160)
        self.assertLessEqual(len(self.text.encode("utf-8")), 20 * 1024)
        for heading in REQUIRED_HEADINGS:
            self.assertEqual(self.text.count(f"## {heading}"), 1, heading)
        self.assertEqual(self.text.count("Authoritative product plan:"), 1)
        self.assertEqual(self.text.count("Historical record:"), 1)

    def test_historical_handoff_is_preserved_exactly(self) -> None:
        data = ARCHIVE.read_bytes().replace(b"\r\n", b"\n")
        digest = hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()
        self.assertEqual(digest, ARCHIVE_SHA1)

    def test_local_links_resolve(self) -> None:
        for target in re.findall(r"\[[^]]+\]\(([^)]+)\)", self.text):
            if "://" not in target:
                self.assertTrue((HANDOFF.parent / target).is_file(), target)

    def test_current_dashboard_excludes_stale_or_private_claims(self) -> None:
        self.assertNotRegex(self.text, r"docs/HANDOFF\.md:\d+")
        self.assertNotIn("cpu_overclock = 100", self.text)
        self.assertNotIn("cpu_overclock = 900", self.text)
        self.assertNotRegex(self.text, r"(?i)PGXP (?:is )?enabled")
        self.assertNotRegex(self.text, r"[A-Za-z]:\\Users\\|/home/[^/]+/")


if __name__ == "__main__":
    unittest.main()
