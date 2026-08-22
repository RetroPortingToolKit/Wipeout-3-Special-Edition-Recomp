import copy
import unittest

from tools.analysis.audit_content_static_coverage import (
    AuditError,
    build_audit,
)

MAIN = "1" * 64
TEXT = "2" * 64
NFE_CONTAINER = "3" * 64
NFE_SOURCE = "4" * 64
NFE_VARIANT = "5" * 64


def fixtures():
    iso = {
        "schema": "psxrecomp.iso9660-inventory.v1", "status": "ok",
        "file_count": 3, "files": [
            {"logical_path": "/SCES_028.45", "bytes": 12,
             "sha256": MAIN, "hash_status": "ok", "signature": "ps-x-exe"},
            {"logical_path": "/WIPEOUT3/NFE.PB", "bytes": 20,
             "sha256": NFE_CONTAINER, "hash_status": "ok", "signature": "none"},
            {"logical_path": "/WIPEOUT3/TRACKM01.PBP", "bytes": 8,
             "sha256": "6" * 64, "hash_status": "ok", "signature": "none"},
        ],
    }
    extraction = {"status": "ok", "extraction": {"file_count": 1, "files": [
        {"logical_path": "/WIPEOUT3/TRACKM01.PBP", "bytes": 8,
         "sha256": "6" * 64}
    ]}}
    expanded = [("/WIPEOUT3/TRACKM01.PBP", {
        "status": "ok", "expanded_verification": {"status": "exact"},
        "container": {"bytes": 8, "sha256": "6" * 64}, "entry_count": 1,
        "entries": [{"index": 0, "encoding": "stored", "expanded_bytes": 4,
                     "expanded_sha256": "7" * 64}],
    }, "8" * 64)]
    nfe_entries = [
        {"index": index, "encoding": "stored", "expanded_bytes": 4,
         "expanded_sha256": NFE_SOURCE if index == 5 else str(index) * 64}
        for index in range(6)
    ]
    nfe = {"status": "ok", "expanded_verification": {"status": "exact"},
           "container": {"bytes": 20, "sha256": NFE_CONTAINER},
           "entry_count": 6, "entries": nfe_entries}
    report = lambda name, base, category: {
        "name": name, "base": base, "end_exclusive": base + 16,
        "authorized_instruction_count": 2,
        "authorized_ranges": [{"start": base, "end_exclusive": base + 8}],
        "direct_closure_complete": False, "finite_indirect_closure": False,
        "rejected_direct_edges": [{"source": base, "target": base + 12}],
        "unresolved_indirects": [{"pc": base + 4, "source_register": 4,
                                  "category": category}],
        "unknown_ranges": [{"start": base + 8, "end_exclusive": base + 16}],
    }
    authorization = {
        "schema": "wo3se.executable-authorization.v1",
        "evidence": {"exe_container_sha256": MAIN, "exe_text_sha256": TEXT,
                     "nfe_entry5_source_sha256": NFE_SOURCE,
                     "nfe_route_variant_sha256": NFE_VARIANT},
        "images": [report("main-exe", 0x1000, "argument_callback_target"),
                   report("nfe-entry5-route-variant", 0x2000,
                          "return_value_target")],
    }
    authorization["images"][0]["unresolved_indirects"].append(
        {"pc": 0x1008, "source_register": 2,
         "category": "return_value_target"})
    finite = {"schema": "wo3se.finite-indirect-targets.v1",
              "image": {"sha256": TEXT},
              "sites": [{"source": 0x1008, "source_register": 2,
                         "proof": "synthetic", "targets": [0x100C]}]}
    epoch = {"schema": "wo3se.runtime-variant-inventory.v1",
             "source": {"file_sha256": NFE_SOURCE},
             "runtime": {"guest_start": 0x2000, "guest_end_exclusive": 0x2008,
                         "compared_bytes": 8, "compared_sha256": "9" * 64},
             "delta": {"changed_word_count": 0},
             "observed_execution": {"pc_count": 2}, "status": "exact-source"}
    return iso, extraction, expanded, nfe, authorization, finite, [(epoch, "a" * 64)]


def build(values):
    iso, extraction, expanded, nfe, authorization, finite, epochs = values
    return build_audit(iso, "b" * 64, extraction, "c" * 64, expanded,
                       nfe, "d" * 64, authorization, "e" * 64,
                       finite, "f" * 64, epochs)


class ContentCoverageAuditTests(unittest.TestCase):
    def test_maps_known_images_and_keeps_unknown_payloads_fail_closed(self):
        receipt = build(fixtures())
        self.assertEqual(receipt["summary"]["cataloged_payloads"], 8)
        self.assertEqual(receipt["summary"]["authenticated_executable_payloads"], 2)
        self.assertEqual(receipt["summary"]["unclassified_payloads"], 6)
        self.assertEqual(receipt["summary"]["runtime_epochs"], 1)
        self.assertFalse(receipt["coverage_gates"]["content_static_coverage_complete"])
        main = next(row for row in receipt["executable_images"]
                    if row["name"] == "main-exe")
        self.assertEqual(len(main["callback_sites"]), 1)
        self.assertEqual(len(main["supplemental_finite_sites"]), 1)
        self.assertEqual(len(main["unresolved_indirects"]), 1)
        self.assertEqual(main["compiled_region_count"], 0)

    def test_media_and_executable_identity_mismatches_are_rejected(self):
        cases = []
        bad_pbp = copy.deepcopy(fixtures())
        bad_pbp[1]["extraction"]["files"][0]["sha256"] = "0" * 64
        cases.append(bad_pbp)
        bad_main = copy.deepcopy(fixtures())
        bad_main[4]["evidence"]["exe_container_sha256"] = "0" * 64
        cases.append(bad_main)
        bad_nfe = copy.deepcopy(fixtures())
        bad_nfe[4]["evidence"]["nfe_entry5_source_sha256"] = "0" * 64
        cases.append(bad_nfe)
        for candidate in cases:
            with self.assertRaises(AuditError):
                build(candidate)

    def test_missing_inventory_and_overlapping_epochs_are_rejected(self):
        missing = fixtures()
        missing[2].clear()
        with self.assertRaises(AuditError):
            build(missing)

        overlapping = fixtures()
        second = copy.deepcopy(overlapping[6][0][0])
        second["runtime"]["guest_start"] = 0x2004
        second["runtime"]["guest_end_exclusive"] = 0x200C
        overlapping[6].append((second, "0" * 64))
        with self.assertRaisesRegex(AuditError, "overlap"):
            build(overlapping)


if __name__ == "__main__":
    unittest.main()
