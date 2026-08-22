import tempfile
import unittest
from pathlib import Path

from tools.analysis.advance_live_shell_frontiers import (
    TERMINAL, instruction_span, synchronize_probe_capacity)
from tools.analysis.live_shell_frontier_batch import (
    BatchError, audit_exact_receipt, authenticated_mandatory_ranges,
    authenticated_straight_line_span, receipt_relink_projection)


class LiveShellFrontierTests(unittest.TestCase):
    def test_probe_capacity_tracks_exact_manifest_operations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / "probe.cpp"
            binds = root / "binds.inc"
            probe.write_text(
                "auto bind=[&](auto*x){r.bind_authenticated_copied_epoch(x);};\n"
                "bind(clean_aot_binding_a());\n"
                "r.bind_authenticated_copied_epoch(b);\n"
                "Configuration c{};Runtime r(c);\n",
                encoding="utf-8")
            binds.write_text("first\nsecond\n", encoding="utf-8")
            self.assertEqual(synchronize_probe_capacity(probe, binds), 4)
            self.assertIn("c.authenticated_region_capacity=4U;Runtime r(c);",
                          probe.read_text(encoding="utf-8"))
            self.assertEqual(synchronize_probe_capacity(probe, binds), 4)

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

    def test_mandatory_closure_follows_only_plain_jump(self):
        base = 0x80000000
        jump = (0x08000004).to_bytes(4, "little")
        normal = (0x24020001).to_bytes(4, "little")
        image = jump + normal + normal * 2 + normal * 2
        self.assertEqual(authenticated_mandatory_ranges(image, base, base, 8),
                         [(base, base + 8), (base + 16, base + 24)])

    def test_mandatory_closure_does_not_authorize_conditional_target(self):
        base = 0x80000000
        branch = (0x10000002).to_bytes(4, "little")
        normal = (0x24020001).to_bytes(4, "little")
        self.assertEqual(authenticated_mandatory_ranges(
            branch + normal * 4, base, base, 16), [(base, base + 8)])

    def test_mandatory_closure_does_not_authorize_jal_or_return(self):
        base = 0x80000000
        jal = (0x0c000004).to_bytes(4, "little")
        normal = (0x24020001).to_bytes(4, "little")
        self.assertEqual(authenticated_mandatory_ranges(
            jal + normal * 4, base, base, 16), [(base, base + 8)])

    def test_existing_receipt_projects_fewer_exact_relinks(self):
        root = Path(__file__).resolve().parents[3]
        result = receipt_relink_projection(
            root / "clean-aot/catalog/evidence/bios-shell-live-frontiers-8005161c.json")
        self.assertLess(result["bundle_count"], result["binding_count"])


if __name__ == "__main__":
    unittest.main()
