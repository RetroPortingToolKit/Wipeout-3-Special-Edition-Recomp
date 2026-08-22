import unittest

from tools.analysis.advance_live_shell_frontiers import TERMINAL, instruction_span


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


if __name__ == "__main__":
    unittest.main()
