from __future__ import annotations
import json, tempfile, unittest
from pathlib import Path
from tools.product.generate_epoch_registry import RegistryError, generate

class EpochRegistryTests(unittest.TestCase):
    def test_chain_is_ordered_and_identity_accessor_is_one_to_one(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); evidence=root/"evidence"; generated=root/"generated"; evidence.mkdir(); generated.mkdir()
            ids=["1"*64,"2"*64]
            for count, transitions in ((10,[ids[0]]),(20,[ids[1]])):
                (evidence/f"bios-frontier-{count:08}.json").write_text(json.dumps({
                    "schema":"clean-aot.real-bios-execution.v2","bios_instructions_executed":count,
                    "bios_identity_sha256":"a"*64,"bios_sha256":"b"*64,
                    "transitions":[{"variant_identity_sha256":value} for value in transitions]}))
            for identity in ids:
                directory=generated/identity[:4]; directory.mkdir(); (directory/"region.cpp").write_text("// generated\n")
                (directory/"entry_table.cpp").write_text(
                    f'GeneratedExecutableBinding value{{"{identity}","{"c"*64}",0,4,roots,entries}};\n'
                    f'GeneratedExecutableBinding* clean_aot_binding_{identity}() noexcept;\n')
            receipt=generate(evidence,generated,root/"out")
            self.assertEqual([item["identity_sha256"] for item in receipt["entries"]],ids)
            self.assertEqual(receipt["frontier"]["bios_instructions_executed"],20)
            duplicate=generated/"duplicate"; duplicate.mkdir();
            (duplicate/"entry_table.cpp").write_text((generated/ids[0][:4]/"entry_table.cpp").read_text())
            with self.assertRaisesRegex(RegistryError,"one-to-one"): generate(evidence,generated,root/"bad")

    def test_game_shards_are_exact_and_identity_mutation_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); evidence=root/"evidence"; generated=root/"generated"; evidence.mkdir(); generated.mkdir()
            (evidence/"bios-frontier-1.json").write_text(json.dumps({"schema":"clean-aot.real-bios-execution.v2","bios_instructions_executed":1,"bios_identity_sha256":"a"*64,"bios_sha256":"b"*64,"transitions":[]}))
            identities=["1"*64,"2"*64]
            for identity in identities:
                directory=generated/identity[:4]; directory.mkdir(); (directory/"region.cpp").write_text("// generated\n")
                (directory/"entry_table.cpp").write_text(f'GeneratedExecutableBinding value{{"{identity}","{identity}"\nGeneratedExecutableBinding* clean_aot_binding_{identity}() noexcept;\n')
            authority=root/"game.json"; authority.write_text(json.dumps({"schema":"wo3se.generated-game-shards.v1","status":"partial-fail-closed","compiled_instruction_count":135829,"compiled_region_count":290,"unresolved_cfi_site_count":0,"policy":{"unknown_pc_action":"reject","unknown_identity_action":"reject","authenticated_epoch_variants":True},"epoch_dispatch":{"schema":"wo3se.overlapping-executable-epoch-dispatch.v1","selection_key":"authenticated-active-image-sha256","unknown_epoch_action":"reject","sites":[0x1000],"variants":[{"image":name,"identity_sha256":value} for name,value in zip(("main-exe","nfe"),identities)]},"images":[{"name":name,"identity_sha256":value,"authorized_instruction_count":count,"compiled_instruction_count":count,"excluded_instruction_count":0,"compiled_region_count":regions} for name,value,count,regions in zip(("main-exe","nfe"),identities,(104002,31827),(272,18))]}))
            receipt=generate(evidence,generated,root/"out",authority)
            self.assertEqual(len(receipt["game_shards"]["entries"]),2)
            self.assertEqual(receipt["game_shards"]["compiled_instruction_count"],135829)
            self.assertEqual(receipt["game_shards"]["compiled_region_count"],290)
            self.assertEqual(receipt["game_shards"]["unresolved_cfi_site_count"],0)
            (generated/identities[0][:4]/"entry_table.cpp").write_text((generated/identities[0][:4]/"entry_table.cpp").read_text().replace(identities[0],"3"*64,1))
            with self.assertRaises(RegistryError): generate(evidence,generated,root/"mutated",authority)

if __name__ == "__main__": unittest.main()
