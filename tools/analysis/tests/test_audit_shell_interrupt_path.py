import copy
import json
import struct
import unittest

from tools.analysis.audit_shell_interrupt_path import AuditError, build, digest


class ShellInterruptAuditTests(unittest.TestCase):
    def fixture(self):
        bios = bytearray(0xD8EC)
        struct.pack_into("<3I", bios, 0xD8E0, 0x240A00A0, 0x01400008, 0x24090040)
        ram = bytearray(0x10000)
        struct.pack_into("<4I", ram, 0x80, 0x3C1A0000, 0x275A0C80, 0x03400008, 0)
        struct.pack_into("<4I", ram, 0xA0, 0x3C080000, 0x250805C4, 0x01000008, 0)
        struct.pack_into("<7I", ram, 0x5C4, 0x24080200, 0x00094880, 0x01094020,
                         0x8D080000, 0, 0x01000008, 0)
        struct.pack_into("<I", ram, 0x300, 0xBFC0D8E0)
        route = {"schema": "clean-aot-shell-disc-route-execution-v1",
                 "terminal": {"pc": "0x000005C4", "dispatch_status": "Executed",
                              "instructions": 1, "dispatches": 2}}
        runtime = b"soc.interrupts().raise(soc::InterruptSource::vblank) soc.interrupts().write_status(written) sample_device_irqs(); interrupt_lines = soc.interrupts().cpu_line() ? 1U : 0U cop0_cause=(impl_->bios_cpu.cop0_cause&~0x0000FC00U)"
        soc = b"status_ &= value & kInterruptAllMask return (status_ & mask_) != 0U"
        generated = b"sample_interrupt(GeneratedCpuState&c) const auto mode=effective_status(c)&0x3FU ((mode<<2U)&0x3CU) rfe(GeneratedCpuState&c) ((mode>>2U)&0xFU)"
        spec = {"schema": "clean-aot-shell-interrupt-audit-spec-v1",
                "identities": {"bios_sha256": digest(bios),
                               "ram_epoch_sha256": digest(ram)}}
        return bytes(bios), bytes(ram), route, b"route", runtime, soc, generated, spec

    def test_exact_path_is_not_exception_vector(self):
        receipt = build(*self.fixture())
        self.assertEqual(receipt["classification"],
                         "a0-service-dispatch-not-interrupt-handler")
        self.assertEqual(receipt["exception_path"], ["0x00000080", "0x00000C80"])

    def test_rejects_byte_identity_and_terminal_mutation(self):
        args = list(self.fixture())
        mutated = bytearray(args[1]); mutated[0x5C4] ^= 1; args[1] = bytes(mutated)
        with self.assertRaises(AuditError): build(*args)
        args = list(self.fixture()); args[2] = copy.deepcopy(args[2]);
        args[2]["terminal"]["pc"] = "0x00000080"
        with self.assertRaises(AuditError): build(*args)


if __name__ == "__main__":
    unittest.main()
