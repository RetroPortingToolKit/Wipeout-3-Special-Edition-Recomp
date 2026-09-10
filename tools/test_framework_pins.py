import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import framework_pins

GOOD_PINS = """\
psxrecomp=1111111 (1111111111111111111111111111111111111111)
recomp-ui=2222222 (2222222222222222222222222222222222222222)
recomp-net=3333333 (3333333333333333333333333333333333333333)
retcomm-rbengine=4444444 (4444444444444444444444444444444444444444)
"""


class FrameworkPinsTests(unittest.TestCase):
    def test_parser_accepts_canonical_snapshot(self):
        pins, errors = framework_pins.parse_framework_pins(GOOD_PINS)
        self.assertEqual(errors, [])
        self.assertEqual([pin.name for pin in pins], [name for name, _ in framework_pins.PIN_PATHS])

    def test_parser_requires_exact_order_and_key_set(self):
        lines = GOOD_PINS.splitlines()
        lines[0], lines[1] = lines[1], lines[0]
        _, errors = framework_pins.parse_framework_pins("\n".join(lines) + "\n")
        self.assertTrue(any("line 1 must be psxrecomp" in error for error in errors))
        self.assertTrue(any("line 2 must be recomp-ui" in error for error in errors))

    def test_parser_rejects_non_prefix_short_sha(self):
        _, errors = framework_pins.parse_framework_pins(
            GOOD_PINS.replace("psxrecomp=1111111", "psxrecomp=abcdef0")
        )
        self.assertTrue(any("not a prefix" in error for error in errors))

    def test_current_repository_snapshot_matches_gitlinks(self):
        root = Path(__file__).resolve().parents[1]
        text = (root / "framework_pins.txt").read_text(encoding="utf-8")
        self.assertEqual(
            framework_pins.validate_authoritative_framework_pins(text, root), []
        )


if __name__ == "__main__":
    unittest.main()
