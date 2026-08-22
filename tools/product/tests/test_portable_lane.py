from __future__ import annotations

import tempfile
import unittest
import json
import shutil
from pathlib import Path

from tools.product.build_product import _build_parser
from tools.product.installer_lane import audit_installed_runtime
from tools.product.release_lane import ReleaseError, create_release, validate_release


class PortableLaneTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.app = self.root / "Wipeout3SE.exe"; self.app.write_bytes(b"MZ clean app")
        self.sdl = self.root / "SDL3.dll"; self.sdl.write_bytes(b"MZ sdl")
        self.region = self.root / "region.dat"; self.region.write_bytes(b"clean generated region")
        self.source = self.root / "product.cpp"; self.source.write_text("int clean_entry(){return 0;}\n")
        self.link = self.root / "product.map"; self.link.write_text(
            " Timestamp is 00000000\n Preferred load address is 0000000140000000\n"
            " Start Length Name Class\n 0001:00000000 00000010H .text CODE\n"
            " Address Publics by Value Rva+Base Lib:Object\n"
            " 0001:00000000 clean_entry 0000000140001000 f app.obj\n")

    def tearDown(self): self.temp.cleanup()

    def package(self, output: Path) -> None:
        create_release(app=self.app, sdl=self.sdl,
            artifacts=[("regions/main.dat", self.region)], output=output,
            root=self.root, link_map=self.link, sources=[self.source])

    def test_single_entrypoint_portable_package_has_no_setup_tooling(self):
        package = self.root / "portable"; self.package(package)
        receipt = validate_release(package)
        self.assertEqual(receipt["status"], "validated")
        self.assertTrue((package / "Wipeout3SE.exe").is_file())
        self.assertTrue((package / "SDL3.dll").is_file())
        self.assertFalse((package / "Wipeout3SE.cmd").exists())
        self.assertFalse((package / "Wipeout3SE-Setup.exe").exists())
        self.assertEqual(audit_installed_runtime(package)["status"], "pass")

    def test_setup_or_compiler_executable_is_rejected(self):
        package = self.root / "portable"; self.package(package)
        extra = package / "Wipeout3SE-Setup.exe"; extra.write_bytes(b"MZ")
        with self.assertRaisesRegex(ReleaseError, "unmanifested"):
            validate_release(package)

    def test_cli_exposes_portable_acceptance_and_retires_installer(self):
        parser = _build_parser()
        choices = parser._subparsers._group_actions[0].choices
        self.assertIn("portable-acceptance", choices)
        self.assertNotIn("package-installer", choices)
        self.assertNotIn("installer-acceptance", choices)
        self.assertNotIn("validate-installer", choices)

    def test_unicode_relocation_remeasures_identically(self):
        package = self.root / "portable"; self.package(package)
        relocated = self.root / "利用者" / "ゲーム"
        relocated.parent.mkdir(parents=True)
        shutil.copytree(package, relocated)
        self.assertEqual(validate_release(package)["manifest_sha256"],
                         validate_release(relocated)["manifest_sha256"])

    def test_delivery_contract_mutation_fails_closed(self):
        package = self.root / "portable"; self.package(package)
        manifest_path = package / "release-manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["delivery"]["registry_installation"] = True
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ReleaseError, "delivery contract"):
            validate_release(package)

    def test_compiler_named_generated_artifact_is_rejected(self):
        with self.assertRaisesRegex(ReleaseError, "forbidden release payload"):
            create_release(app=self.app, sdl=self.sdl,
                artifacts=[("compiler/cache.dat", self.region)], output=self.root / "bad",
                root=self.root, link_map=self.link, sources=[self.source])


if __name__ == "__main__": unittest.main()
