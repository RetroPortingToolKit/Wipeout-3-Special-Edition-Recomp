import hashlib
import struct
import unittest

from tools.analysis.build_indirect_cfi import CfiError, build


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


if __name__ == "__main__":
    unittest.main()
