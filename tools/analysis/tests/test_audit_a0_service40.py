import copy
import struct
import unittest

from tools.analysis.audit_a0_service40 import AuditError, build, digest


class A0Service40AuditTests(unittest.TestCase):
    def fixture(self):
        bios = bytearray(0xD8EC); ram = bytearray(0x10000); full = bytearray(0x80000)
        struct.pack_into("<3I", bios, 0xD8E0, 0x240A00A0, 0x01400008, 0x24090040)
        struct.pack_into("<4I", ram, 0xA0, 0x3C080000, 0x250805C4, 0x01000008, 0)
        struct.pack_into("<7I", ram, 0x5C4, 0x24080200, 0x00094880, 0x01094020, 0x8D080000, 0, 0x01000008, 0)
        struct.pack_into("<I", ram, 0x300, 0xBFC0D8E0)
        struct.pack_into("<3I", ram, 0x6A80, 0x240A00A0, 0x01400008, 0x24090040)
        struct.pack_into("<2I", ram, 0x1B08, 0x0C001AA0, 0)
        struct.pack_into("<7I", full, 0x51040, 0x8C6392A0, 0x34211000, 0x31AE3FFF, 0x01C17825, 0xAC6F0000, 0x8C600000, 0x8C580000)
        struct.pack_into("<I", full, 0x792A0, 0x1F801810)
        route = {"schema": "clean-aot-shell-disc-route-execution-v1", "terminal": {"pc": "0x000005C4"}}
        runtime = b"return result.ready ? handled_read(result.word) : rejected_read();"
        spec = {"schema": "clean-aot-a0-service40-audit-spec-v1", "identities": {
            "bios_sha256": digest(bios), "ram_epoch_sha256": digest(ram), "full_ram_sha256": digest(full)}}
        return bytes(bios), bytes(ram), bytes(full), route, b"route", runtime, spec

    def test_proves_nonreturning_cycle_and_trigger(self):
        out = build(*self.fixture())
        self.assertFalse(out["service"]["returns"])
        self.assertTrue(out["finding"]["runtime_defect_proven"])

    def test_fail_closed_on_table_or_route_mutation(self):
        args = list(self.fixture()); ram = bytearray(args[1]); ram[0x300] ^= 1; args[1] = bytes(ram)
        with self.assertRaises(AuditError): build(*args)
        args = list(self.fixture()); args[3] = copy.deepcopy(args[3]); args[3]["terminal"]["pc"] = "0x80"
        with self.assertRaises(AuditError): build(*args)


if __name__ == "__main__":
    unittest.main()
