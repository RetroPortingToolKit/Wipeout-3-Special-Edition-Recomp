import unittest
from pathlib import Path

from tools.analysis.advance_live_shell_frontiers import TERMINAL, instruction_span
from tools.analysis.live_shell_frontier_batch import (
    BatchError, audit_exact_receipt, authenticated_straight_line_span)


class LiveShellFrontierTests(unittest.TestCase):
    def test_delay_slot_is_atomic_only_for_control_transfer(self):
        normal = (0x24020001).to_bytes(4, "little")
        jump = (0x08000010).to_bytes(4, "little") + bytes(4)
        indirect = (0x03E00008).to_bytes(4, "little") + bytes(4)
        self.assertEqual(instruction_span(normal, 0), 4)
        self.assertEqual(instruction_span(jump, 0), 8)
        self.assertEqual(instruction_span(indirect, 0), 8)

    def test_terminal_receipt_extracts_cd_counters(self):
        line = "19272155 1787780 2 2147816988 397 25093 4230987 2992558 7680 5011 56408 54328 0 0 1025 0 2147854872 0\n"
        match = TERMINAL.search(line)
        self.assertIsNotNone(match)
        self.assertEqual(tuple(map(int, match.groups())),
                         (19272155, 1787780, 2, 2147816988, 0, 0))

    def test_straight_line_batch_stops_before_later_control_transfer(self):
        normal = (0x24020001).to_bytes(4, "little")
        jump = (0x08000010).to_bytes(4, "little")
        image = normal * 3 + jump + normal
        self.assertEqual(authenticated_straight_line_span(image, 0, 8), 12)

    def test_frontier_control_transfer_keeps_delay_slot_atomic(self):
        jump = (0x08000010).to_bytes(4, "little")
        normal = (0x24020001).to_bytes(4, "little")
        self.assertEqual(authenticated_straight_line_span(jump + normal * 3, 0, 8), 8)

    def test_batch_is_bounded_and_rejects_missing_delay_slot(self):
        normal = (0x24020001).to_bytes(4, "little")
        self.assertEqual(authenticated_straight_line_span(normal * 3, 0, 2), 8)
        with self.assertRaises(BatchError):
            authenticated_straight_line_span((0x08000010).to_bytes(4, "little"), 0, 8)

    def test_committed_frontier_receipt_has_exact_identity_equivalence(self):
        root = Path(__file__).resolve().parents[3]
        result = audit_exact_receipt(
            root / "clean-aot/catalog/evidence/bios-shell-live-frontiers-8005161c.json")
        self.assertEqual(result["binding_count"], 103)
        self.assertEqual(result["instruction_count"], 116)


if __name__ == "__main__":
    unittest.main()
