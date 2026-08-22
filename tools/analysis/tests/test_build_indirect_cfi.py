import hashlib
import struct
import unittest

from tools.analysis.build_indirect_cfi import (
    CfiError,
    bounded_table_targets,
    build,
    fixed_dispatch_targets,
    memory_loaded_callback,
    renderer_callback_contract,
)


class IndirectCfiTests(unittest.TestCase):
    def fixture(self):
        data = bytearray(20)
        struct.pack_into("<I", data, 0, 0x0C000404)  # jal 0x1010
        struct.pack_into("<I", data, 8, 0x03E00008)  # jr ra
        struct.pack_into("<I", data, 12, 0x00800008)  # jr a0
        digest = hashlib.sha256(data).hexdigest()
        image = {"name": "main-exe", "base": 0x1000, "end_exclusive": 0x1014,
                 "roots": [0x1000],
                 "authorized_ranges": [{"start": 0x1000, "end_exclusive": 0x1014}],
                 "direct_edges": [{"source": 0x1000, "target": 0x1010,
                                    "kind": "direct_call"},
                                  {"source": 0x1000, "target": 0x1008,
                                   "kind": "fallthrough"}],
                 "unresolved_indirects": [{"pc": 0x100C, "source_register": 4,
                                           "category": "argument_callback_target"}]}
        nfe = bytes(4)
        nfe_digest = hashlib.sha256(nfe).hexdigest()
        nfe_image = {"name": "nfe-entry5-route-variant", "base": 0x2000,
                     "end_exclusive": 0x2004, "roots": [0x2000],
                     "authorized_ranges": [{"start": 0x2000, "end_exclusive": 0x2004}],
                     "direct_edges": [], "unresolved_indirects": []}
        authority = {"schema": "wo3se.executable-authorization.v1",
                     "evidence": {"exe_text_sha256": digest,
                                  "nfe_route_variant_sha256": nfe_digest},
                     "images": [image, nfe_image]}
        shards = {"schema": "wo3se.generated-game-shards.v1", "images": [
            {"name": "main-exe", "ranges": [{"start": 0x1000, "end_exclusive": 0x1014}]},
            {"name": "nfe-entry5-route-variant",
             "ranges": [{"start": 0x2000, "end_exclusive": 0x2004}]},
        ]}
        return authority, shards, {"main-exe": bytes(data),
                                    "nfe-entry5-route-variant": nfe}

    def test_returns_and_callbacks_use_distinct_finite_universes(self):
        result = build(*self.fixture())
        universes = {row["id"]: row["entries"] for row in result["universes"]}
        self.assertEqual(universes["main-exe:function-entry"], [0x1000, 0x1010])
        self.assertEqual(universes["main-exe:call-continuation"], [0x1008])
        policies = {(row["source_pc"], row["kind"]): row for row in result["sites"]}
        self.assertEqual(policies[(0x1008, "return")]["universe"],
                         "main-exe:call-continuation")
        self.assertTrue(policies[(0x100C, "bounded-callback")]["admit_source"])

    def test_wrong_image_identity_is_rejected(self):
        authority, shards, images = self.fixture()
        images["main-exe"] = bytes(len(images["main-exe"]))
        with self.assertRaises(CfiError):
            build(authority, shards, images)

    def test_bounded_table_requires_exact_guard_and_authenticated_table(self):
        base = 0x10000000
        data = bytearray(0x100)
        pc = base + 0x20
        words = [
            0x2C820003,  # sltiu v0, a0, 3
            0x10400008,  # beq v0, zero, default
            0x00042080,  # sll a0, a0, 2
            0x3C021000,  # lui v0, 0x1000
            0x00441021,  # addu v0, v0, a0
            0x8C420080,  # lw v0, 0x80(v0)
            0x00000000,  # load delay
            0x00400008,  # jr v0
        ]
        for index, raw in enumerate(words):
            struct.pack_into("<I", data, pc - base - 28 + index * 4, raw)
        targets = (base + 0x40, base + 0x50, base + 0x60)
        for index, target in enumerate(targets):
            struct.pack_into("<I", data, 0x80 + index * 4, target)
        self.assertEqual(bounded_table_targets(bytes(data), base, pc, 2),
                         list(targets))
        data[pc - base - 20] ^= 1
        self.assertIsNone(bounded_table_targets(bytes(data), base, pc, 2))

    def test_memory_callback_requires_nearest_definition_to_be_lw(self):
        base = 0x1000
        data = bytearray(0x40)
        struct.pack_into("<I", data, 0, 0x8E220010)  # lw v0, 16(s1)
        self.assertTrue(memory_loaded_callback(bytes(data), base, 0x1008, 2))
        struct.pack_into("<I", data, 4, 0x24020001)  # addiu v0, zero, 1
        self.assertFalse(memory_loaded_callback(bytes(data), base, 0x1008, 2))

    def test_fixed_dispatcher_rejects_near_match(self):
        base = 0x8016FC00
        pc = 0x8016FD2C
        data = bytearray(0x200)
        expected = {
            0x8016FC48: 0x3C0C8016, 0x8016FC4C: 0x358CF94C,
            0x8016FC50: 0x3C0D8016, 0x8016FC54: 0x35ADFA70,
            0x8016FC58: 0x3C0E8017, 0x8016FC5C: 0x35CE0774,
            0x8016FCA0: 0x01A06021, 0x8016FCB0: 0x01C06021,
            0x8016FCC4: 0x01A06021, 0x8016FCD4: 0x01A06021,
        }
        for address, raw in expected.items():
            struct.pack_into("<I", data, address - base, raw)
        self.assertEqual(fixed_dispatch_targets(bytes(data), base, pc, 12),
                         [0x8016F94C, 0x8016FA70, 0x80170774])
        struct.pack_into("<I", data, 0x8016FCC4 - base, 0)
        self.assertIsNone(fixed_dispatch_targets(bytes(data), base, pc, 12))

    def test_renderer_inherited_callback_requires_exact_save_restore(self):
        base = 0x80160000
        data = bytearray(0x11000)
        struct.pack_into("<I", data, 0x8017025C - base, 0x03600013)
        struct.pack_into("<I", data, 0x801704EC - base, 0x0000D812)
        self.assertEqual(renderer_callback_contract(
            bytes(data), base, 0x8016FB9C, 27),
            ("bounded-inherited-callback", None))
        struct.pack_into("<I", data, 0x801704EC - base, 0)
        self.assertIsNone(renderer_callback_contract(
            bytes(data), base, 0x8016FB9C, 27))


if __name__ == "__main__":
    unittest.main()
