import copy
import unittest

from tools.analysis.cluster_content_payloads import ClusterError, build_clusters


def fixture():
    return {
        "schema": "wo3se.content-static-coverage.v1",
        "media_payloads": [
            {"classification": "unclassified-payload", "container": "/A/X.PBP",
             "expanded_bytes": 8, "expanded_sha256": "1" * 64,
             "prefix_hex": "424f445900000000"},
            {"classification": "unclassified-payload", "container": "/B/Y.PBP",
             "expanded_bytes": 8, "expanded_sha256": "1" * 64,
             "prefix_hex": "424f445900000000"},
            {"classification": "authenticated-structured-envelope",
             "container": "/A/NFE.PB", "expanded_bytes": 12,
             "expanded_sha256": "2" * 64, "prefix_hex": "0102030400000000"},
            {"classification": "authenticated-data-format", "container": "/A/T.PBP",
             "expanded_bytes": 16, "expanded_sha256": "3" * 64,
             "prefix_hex": "1000000002000000"},
        ],
        "summary": {"unclassified_payloads": 3,
                    "unique_unknown_payload_identities": 2},
    }


class PayloadClusterTests(unittest.TestCase):
    def test_clusters_unique_identities_and_frequencies(self):
        receipt = build_clusters(fixture(), "a" * 64)
        self.assertEqual(receipt["summary"], {"instance_count": 3,
                                              "unique_identity_count": 2})
        body = next(row for row in receipt["identities"]
                    if row["expanded_sha256"] == "1" * 64)
        self.assertEqual(body["magic"], "ascii:BODY")
        self.assertEqual(body["occurrences"], 2)
        self.assertFalse(receipt["coverage_gates"]["all_unknown_roles_resolved"])

    def test_ambiguous_identity_and_accounting_fail_closed(self):
        ambiguous = fixture()
        ambiguous["media_payloads"][1]["expanded_bytes"] = 9
        with self.assertRaisesRegex(ClusterError, "ambiguous"):
            build_clusters(ambiguous, "a" * 64)
        wrong_count = fixture()
        wrong_count["summary"]["unclassified_payloads"] = 4
        with self.assertRaisesRegex(ClusterError, "accounting"):
            build_clusters(wrong_count, "a" * 64)
        wrong_unique = fixture()
        wrong_unique["summary"]["unique_unknown_payload_identities"] = 3
        with self.assertRaisesRegex(ClusterError, "unique identity"):
            build_clusters(wrong_unique, "a" * 64)

    def test_missing_prefix_and_bad_hex_fail_closed(self):
        missing = fixture()
        del missing["media_payloads"][0]["prefix_hex"]
        with self.assertRaises(ClusterError):
            build_clusters(missing, "a" * 64)
        malformed = copy.deepcopy(fixture())
        malformed["media_payloads"][0]["prefix_hex"] = "not-hex"
        malformed["media_payloads"][1]["prefix_hex"] = "not-hex"
        with self.assertRaisesRegex(ClusterError, "hexadecimal"):
            build_clusters(malformed, "a" * 64)


if __name__ == "__main__":
    unittest.main()
