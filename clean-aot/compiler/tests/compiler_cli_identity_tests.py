import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class CompilerCliIdentityTests(unittest.TestCase):
    def test_variant_symbol_namespace_is_exact_and_rejects_unknown_modes(self):
        cli = Path(sys.argv[1])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "image.bin"
            payload = bytes.fromhex("00000000")
            image_digest = hashlib.sha256(payload).hexdigest()
            variant_digest = hashlib.sha256(b"epoch-exact-context").hexdigest()
            image.write_bytes(payload)

            def write_manifest(path, namespace):
                path.write_text("\n".join((
                    "schema=clean-aot-compiler-manifest-v1",
                    "image_kind=raw-payload",
                    f"symbol_namespace={namespace}",
                    f"variant_sha256={variant_digest}",
                    f"image_sha256={image_digest}",
                    "load_start=0x1000",
                    "load_end_exclusive=0x1004",
                    "entry=0x1000",
                    "code=0x1000-0x1004",
                    "",
                )))

            manifest = root / "variant.manifest"
            write_manifest(manifest, "variant")
            output = root / "generated"
            subprocess.run((cli, "--image", image, "--manifest", manifest,
                            "--output", output), check=True, capture_output=True)
            expected = f"clean_aot_region_00001000_v{variant_digest}"
            self.assertTrue((output / f"{expected}.cpp").is_file())
            self.assertIn(expected, (output / "entry_table.cpp").read_text())

            rejected_manifest = root / "rejected.manifest"
            write_manifest(rejected_manifest, "address")
            rejected = subprocess.run(
                (cli, "--image", image, "--manifest", rejected_manifest,
                 "--output", root / "rejected"), capture_output=True, text=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn("unsupported symbol namespace", rejected.stderr)

    def test_identity_bound_emission_and_sparse_unknown_pc(self):
        cli = Path(sys.argv[1])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "image.bin"
            manifest = root / "image.manifest"
            output = root / "generated"
            payload = bytes.fromhex("01000124") + bytes.fromhex("00000000")
            digest = hashlib.sha256(payload).hexdigest()
            image.write_bytes(payload)
            manifest.write_text("\n".join((
                "schema=clean-aot-compiler-manifest-v1",
                "image_kind=raw-payload",
                f"variant_sha256={digest}",
                f"image_sha256={digest}",
                "load_start=0x1000",
                "load_end_exclusive=0x1008",
                "entry=0x1000",
                "code=0x1000-0x1004",
                "",
            )))
            subprocess.run((cli, "--image", image, "--manifest", manifest,
                            "--output", output), check=True, capture_output=True)
            table = (output / "entry_table.cpp").read_text()
            self.assertIn(digest, table)
            self.assertIn("{0x1000U,", table)
            self.assertNotIn("{0x1004U,", table)
            self.assertIn("GeneratedPackedRegionEntry", table)
            self.assertIn("GeneratedDispatchPage", table)
            self.assertEqual(table.count('extern "C" bool clean_aot_region_'), 1)

            changed = root / "changed.bin"
            changed.write_bytes(payload[:-1] + b"\x01")
            rejected = subprocess.run(
                (cli, "--image", changed, "--manifest", manifest,
                 "--output", root / "rejected"), capture_output=True, text=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn("image SHA-256 mismatch", rejected.stderr)

    def test_deterministic_basic_block_shards(self):
        cli = Path(sys.argv[1])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "image.bin"
            manifest = root / "image.manifest"
            output = root / "generated"

            def jump(target):
                return ((2 << 26) | ((target >> 2) & 0x03ffffff)).to_bytes(4, "little")

            payload = b"".join((
                jump(0x1008), b"\0\0\0\0",
                jump(0x1010), b"\0\0\0\0",
                jump(0x1018), b"\0\0\0\0",
                jump(0x1000), b"\0\0\0\0",
            ))
            digest = hashlib.sha256(payload).hexdigest()
            image.write_bytes(payload)
            manifest.write_text("\n".join((
                "schema=clean-aot-compiler-manifest-v1",
                "image_kind=raw-payload",
                f"variant_sha256={digest}",
                f"image_sha256={digest}",
                "load_start=0x1000",
                "load_end_exclusive=0x1020",
                "entry=0x1000",
                "code=0x1000-0x1020",
                "",
            )))
            command = (cli, "--image", image, "--manifest", manifest,
                       "--output", output, "--max-shard-instructions", "2")
            subprocess.run(command, check=True, capture_output=True)
            sources = sorted(output.glob("clean_aot_region_*.cpp"))
            self.assertEqual(len(sources), 4)
            table = (output / "entry_table.cpp").read_text()
            self.assertEqual(table.count('extern "C" bool clean_aot_region_'), 4)
            receipt = (output / "shards.manifest").read_text()
            self.assertIn("schema=clean-aot-generated-shards-v1", receipt)
            self.assertIn("entry_count=4\nshard_count=4", receipt)

            second = root / "generated-again"
            second_command = list(command)
            second_command[second_command.index(output)] = second
            subprocess.run(second_command, check=True, capture_output=True)
            self.assertEqual(receipt, (second / "shards.manifest").read_text())
            self.assertEqual(
                [(path.name, path.read_bytes()) for path in sources],
                [(path.name, path.read_bytes())
                 for path in sorted(second.glob("clean_aot_region_*.cpp"))])

            unsharded = root / "generated-unsharded"
            subprocess.run(
                (cli, "--image", image, "--manifest", manifest,
                 "--output", unsharded, "--max-shard-instructions", "65535"),
                check=True, capture_output=True)

            def compile_and_run(generated, name):
                generated_sources = sorted(generated.glob("clean_aot_region_*.cpp"))
                addresses = [path.stem.removeprefix("clean_aot_region_")
                             for path in generated_sources]
                declarations = "\n".join(
                    f'extern "C" bool clean_aot_region_{address}(clean_aot::runtime::GeneratedCpuState&) noexcept;'
                    for address in addresses)
                selections = "\n".join(
                    f'if(!ok)ok=clean_aot_region_{address}(state);'
                    for address in addresses)
                harness = root / f"{name}.cpp"
                harness.write_text(
                    '#include <clean_aot/runtime/generated_cpu.hpp>\n#include <iostream>\n' +
                    declarations + '\nint main(){clean_aot::runtime::GeneratedCpuState state{};'
                    'state.pc=0x1000U;for(int i=0;i<4;++i){bool ok=false;' +
                    selections +
                    'if(!ok)return 4;}std::cout<<std::hex<<state.pc<<":"'
                    '<<state.gpr[31]<<"\\n";}\n')
                executable = root / f"{name}.exe"
                runtime_include = Path(__file__).parents[2] / "runtime" / "include"
                subprocess.run(
                    (Path(sys.argv[2]), "-std=c++20", "-O0", f"-I{runtime_include}",
                     *generated_sources, harness, "-o", executable),
                    check=True, capture_output=True)
                return subprocess.run((executable,), check=True, capture_output=True,
                                      text=True).stdout

            self.assertEqual(compile_and_run(output, "sharded"),
                             compile_and_run(unsharded, "unsharded"))

            verifier = Path(__file__).parent.parent / "tools" / "verify_generated_shards.py"
            subprocess.run((sys.executable, verifier, output), check=True,
                           capture_output=True)
            second_table = second / "entry_table.cpp"
            second_table.write_bytes(second_table.read_bytes() + b"\n")
            table_tampered = subprocess.run(
                (sys.executable, verifier, second), capture_output=True, text=True)
            self.assertEqual(table_tampered.returncode, 2)
            self.assertIn("entry table source hash mismatch", table_tampered.stderr)
            sources[0].write_bytes(sources[0].read_bytes() + b"\n")
            tampered = subprocess.run(
                (sys.executable, verifier, output), capture_output=True, text=True)
            self.assertEqual(tampered.returncode, 2)
            self.assertIn("source hash mismatch", tampered.stderr)

            rejected = subprocess.run(
                (cli, "--image", image, "--manifest", manifest,
                 "--output", root / "rejected", "--max-shard-instructions", "0"),
                capture_output=True, text=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn("invalid maximum shard", rejected.stderr)


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1])
