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

if __name__ == "__main__": unittest.main()
