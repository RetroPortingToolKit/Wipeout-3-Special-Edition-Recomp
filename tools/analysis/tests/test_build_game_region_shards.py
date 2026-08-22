import unittest

from tools.analysis.build_game_region_shards import ShardError, build, manifest


def image(name, base, *, rejected=(), unresolved=(), cross=()):
    return {
        "name": name, "base": base, "end_exclusive": base + 16,
        "authorized_ranges": [{"start": base, "end_exclusive": base + 16}],
        "direct_edges": [
            {"source": base, "target": base + 4, "kind": "linear"},
            {"source": base + 4, "target": base + 8, "kind": "linear"},
        ],
        "cross_image_direct_edges": list(cross),
        "epoch_discriminated_edges": [],
        "rejected_direct_edges": list(rejected),
        "unresolved_indirects": list(unresolved),
        "delay_slot_owners": [],
    }


class GameShardTests(unittest.TestCase):
    def authority(self):
        main = image("main-exe", 0x1000)
        nfe = image("nfe-entry5-route-variant", 0x2000)
        return {
            "schema": "wo3se.executable-authorization.v1",
            "evidence": {"exe_text_sha256": "a" * 64,
                         "nfe_route_variant_sha256": "b" * 64},
            "images": [main, nfe],
        }

    def finite(self):
        return {"schema": "wo3se.finite-indirect-targets.v1",
                "image": {"name": "main-exe", "sha256": "a" * 64}, "sites": []}

    def test_reverse_closure_excludes_edges_reaching_unknown_code(self):
        authority = self.authority()
        authority["images"][0]["unresolved_indirects"] = [{"pc": 0x1008}]
        result = build(authority, self.finite())
        main = next(x for x in result["images"] if x["name"] == "main-exe")
        self.assertEqual(main["compiled_instruction_count"], 1)
        self.assertEqual(main["entries"], [0x100C])

    def test_cross_image_dependency_is_identity_closed(self):
        authority = self.authority()
        authority["images"][1]["cross_image_direct_edges"] = [
            {"source": 0x2000, "target": 0x1008, "target_image": "main-exe"}]
        authority["images"][0]["unresolved_indirects"] = [{"pc": 0x1008}]
        result = build(authority, self.finite())
        nfe = next(x for x in result["images"] if x["name"].startswith("nfe"))
        self.assertNotIn(0x2000, nfe["entries"])

    def test_epoch_discriminated_source_is_not_compiled_in_wrong_identity(self):
        authority = self.authority()
        authority["images"][0]["epoch_discriminated_edges"] = [
            {"source": 0x1008, "target": 0x100C}]
        result = build(authority, self.finite())
        main = next(x for x in result["images"] if x["name"] == "main-exe")
        self.assertNotIn(0x1008, main["entries"])

    def test_cfi_callback_source_is_admitted_only_with_exact_identity(self):
        authority = self.authority()
        authority["images"][0]["unresolved_indirects"] = [
            {"pc": 0x1008, "source_register": 4,
             "category": "argument_callback_target"}]
        import hashlib
        from tools.analysis.build_game_region_shards import canonical
        cfi = {"schema": "wo3se.indirect-cfi.v1",
               "authority_sha256": hashlib.sha256(canonical(authority)).hexdigest(),
               "sites": [{"admit_source": True, "kind": "bounded-callback",
                          "source_image": "main-exe", "source_pc": 0x1008,
                          "source_identity_sha256": "a" * 64}]}
        result = build(authority, self.finite(), cfi)
        main = next(x for x in result["images"] if x["name"] == "main-exe")
        self.assertIn(0x1008, main["entries"])
        cfi["sites"][0]["source_identity_sha256"] = "b" * 64
        with self.assertRaises(ShardError):
            build(authority, self.finite(), cfi)

    def test_finite_cfi_target_must_be_authorized_and_is_precompiled(self):
        authority = self.authority()
        authority["images"][0]["unresolved_indirects"] = [{"pc": 0x1008}]
        import hashlib
        from tools.analysis.build_game_region_shards import canonical
        cfi = {"schema": "wo3se.indirect-cfi.v1",
               "authority_sha256": hashlib.sha256(canonical(authority)).hexdigest(),
               "universes": [{"id": "main-exe:site-00001008",
                              "identity_sha256": "a" * 64,
                              "entries": [0x100C]}],
               "sites": [{"admit_source": True, "kind": "finite-table",
                          "source_image": "main-exe", "source_pc": 0x1008,
                          "source_identity_sha256": "a" * 64,
                          "universe": "main-exe:site-00001008"}]}
        result = build(authority, self.finite(), cfi)
        main = next(x for x in result["images"] if x["name"] == "main-exe")
        self.assertIn(0x1008, main["entries"])
        self.assertIn(0x100C, main["entries"])
        cfi["universes"][0]["entries"] = [0x1010]
        with self.assertRaises(ShardError):
            build(authority, self.finite(), cfi)

    def test_identity_mismatch_and_zero_output_fail_closed(self):
        finite = self.finite()
        finite["image"]["sha256"] = "c" * 64
        with self.assertRaises(ShardError):
            build(self.authority(), finite)

    def test_manifest_has_exact_entries_and_ranges(self):
        result = build(self.authority(), self.finite())
        text = manifest(result["images"][0])
        self.assertIn("entry=0x00001000", text)
        self.assertIn("code=0x00001000-0x00001010", text)
        self.assertIn("image_sha256=" + "a" * 64, text)


if __name__ == "__main__":
    unittest.main()
