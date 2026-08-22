import hashlib
import struct
import unittest

from tools.analysis.audit_unknown_code_ranges import AuditError, build


class UnknownCodeAuditTests(unittest.TestCase):
    def fixture(self):
        main = bytearray(16)
        struct.pack_into("<I", main, 0, 0x08000402)  # j 0x1008
        digest = hashlib.sha256(main).hexdigest()
        nfe = bytes(4)
        nfe_digest = hashlib.sha256(nfe).hexdigest()
        authority = {"schema": "wo3se.executable-authorization.v1",
                     "evidence": {"exe_text_sha256": digest,
                                  "nfe_route_variant_sha256": nfe_digest},
                     "images": [
                         {"name": "main-exe", "base": 0x1000, "end_exclusive": 0x1010,
                          "authorized_ranges": [{"start": 0x1000, "end_exclusive": 0x1008}],
                          "unknown_ranges": [{"start": 0x1008, "end_exclusive": 0x1010}],
                          "cross_image_direct_edges": []},
                         {"name": "nfe-entry5-route-variant", "base": 0x2000,
                          "end_exclusive": 0x2004, "authorized_ranges": [],
                          "unknown_ranges": [{"start": 0x2000, "end_exclusive": 0x2004}]},
                     ]}
        return authority, {"main-exe": bytes(main),
                           "nfe-entry5-route-variant": nfe}

    def test_reports_authenticated_direct_edge_without_promoting_target(self):
        result = build(*self.fixture())
        self.assertEqual(result["status"], "authenticated-direct-roots-found")
        self.assertEqual(result["authenticated_incoming_direct_edges"], [
            {"image": "main-exe", "source_pc": 0x1000, "target_pc": 0x1008}])
        self.assertEqual(result["recoverable_instruction_count"], 0)
        self.assertFalse(result["policy"]["legal_words_create_roots"])

    def test_identity_selected_cross_edge_is_not_an_unknown_same_image_root(self):
        authority, images = self.fixture()
        authority["images"][0]["cross_image_direct_edges"] = [
            {"source": 0x1000, "target": 0x1008,
             "target_image": "nfe-entry5-route-variant"}]
        result = build(authority, images)
        self.assertEqual(result["status"], "no-additional-authenticated-roots")

    def test_wrong_identity_rejects(self):
        authority, images = self.fixture()
        images["main-exe"] = bytes(16)
        with self.assertRaises(AuditError):
            build(authority, images)


if __name__ == "__main__":
    unittest.main()
