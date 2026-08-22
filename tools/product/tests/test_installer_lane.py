from __future__ import annotations

import json
import shutil
import tempfile
import unittest
from pathlib import Path

from tools.product.installer_lane import (RUNTIME_AUDIT_SCHEMA, audit_installed_runtime,
                                           create_installer, validate_installer)
from tools.product.release_lane import create_release
from tools.product.setup_frontend import SetupCancelled, install_runtime, run_setup


class InstallerLaneTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
        for name, data in (("app.exe", b"MZapp"), ("SDL3.dll", b"MZsdl"),
                           ("region.dat", b"native"), ("setup.exe", b"MZsetup")):
            (self.root / name).write_bytes(data)
        source = self.root / "shipping.cpp"; source.write_text("int clean_entry() { return 0; }\n")
        link = self.root / "release.map"; link.write_text(
            " Timestamp is 00000000\n Preferred load address is 0000000140000000\n"
            " Start Length Name Class\n 0001:00000000 00000010H .text CODE\n"
            " Address Publics by Value Rva+Base Lib:Object\n"
            " 0001:00000000 clean_entry 0000000140001000 f app.obj\n")
        self.runtime = self.root / "runtime"
        create_release(app=self.root / "app.exe", sdl=self.root / "SDL3.dll",
            artifacts=[("regions/main.dat", self.root / "region.dat")], output=self.runtime,
            root=self.root, link_map=link, sources=[source])
        self.payload = []
        for relative in ("patch/pal_track01.xdelta3", "bin/xdelta3.exe",
                         "bin/clean-aot-compiler.exe", "policy/product-regions.json",
                         "policy/artifacts.json", "policy/product-profile.json",
                         "policy/media-manifest.json", "policy/generated-game-shards.json",
                         "policy/game-shards/main-exe-closed-shards.manifest",
                         "policy/game-shards/nfe-entry5-route-variant-closed-shards.manifest",
                         "native/placeholder.dat"):
            path = self.root / ("input-" + str(len(self.payload)))
            path.write_text("{}" if relative.endswith(".json") else "clean")
            self.payload.append((relative, path))

    def tearDown(self): self.temp.cleanup()

    def test_distribution_relocates_and_runtime_is_independently_clean(self):
        package = self.root / "installer"
        create_installer(setup_exe=self.root / "setup.exe", runtime_payload=self.runtime,
                         setup_payload=self.payload, output=package)
        moved = self.root / "日本語" / "relocated"; moved.parent.mkdir(); shutil.copytree(package, moved)
        receipt = validate_installer(moved)
        self.assertEqual(receipt["runtime_audit"]["schema"], RUNTIME_AUDIT_SCHEMA)
        installed = self.root / "installed" / "Wipeout3SE"
        install_runtime(moved / "runtime-payload", installed)
        self.assertFalse((installed / "setup-payload").exists())
        self.assertFalse((installed / "Wipeout3SE-Setup.exe").exists())
        self.assertEqual(audit_installed_runtime(installed)["status"], "pass")

    def test_cancel_is_side_effect_free_and_unicode_paths_reach_orchestrator(self):
        with self.assertRaises(SetupCancelled):
            run_setup(self.root, self.root / "install", self.root / "user",
                      selector=lambda *_: None, launch=False)
        self.assertFalse((self.root / "install").exists())
        package = self.root / "pkg"; package.mkdir(); shutil.copytree(self.runtime, package / "runtime-payload")
        payload = package / "setup-payload"; payload.mkdir()
        contract = {"patch":"p", "xdelta3":"x", "compiler":"c", "regions":"r",
                    "artifacts":"a", "artifact_root":"n", "profile":"q", "media_manifest":"m",
                    "game_shards":"g", "game_shard_manifest_root":"s"}
        (payload / "setup-contract.json").write_text(json.dumps(contract), encoding="utf-8")
        seen = {}
        def orchestrator(**kwargs): seen.update(kwargs); return {"version":"v"}
        bios = self.root / "媒体" / "bios.bin"; cue = self.root / "媒体" / "ゲーム.cue"
        run_setup(package, self.root / "設置" / "Wipeout3SE", self.root / "利用者",
                  bios=bios, cue=cue, orchestrator=orchestrator, launch=False)
        self.assertEqual(seen["bios"], bios); self.assertEqual(seen["cue"], cue)

    def test_failed_runtime_copy_rolls_back_existing_install(self):
        destination = self.root / "existing"; destination.mkdir(); (destination / "old.txt").write_text("old")
        def fail_copy(_source, staging):
            staging.mkdir(); (staging / "partial").write_text("bad"); raise OSError("injected")
        with self.assertRaisesRegex(OSError, "injected"):
            install_runtime(self.runtime, destination, copier=fail_copy)
        self.assertEqual((destination / "old.txt").read_text(), "old")


if __name__ == "__main__": unittest.main()
