from __future__ import annotations

import hashlib
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

from tools.product.release_lane import (
    CONFIG_SCHEMA,
    ReleaseError,
    create_release,
    initialize_install,
    run_soak,
    validate_release,
)
from tools.product.install_lane import (ACTIVE_SCHEMA, InstallError, generation_lock,
                                        launch_and_record)
from tools.product.pipeline import canonical_json_bytes


class ReleaseLaneTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        (self.root / "src").mkdir()
        (self.root / "build").mkdir()
        self.source = self.root / "src" / "shipping.cpp"
        self.source.write_text("int shipping_entry() { return 0; }\n", encoding="utf-8")
        self.link_map = self.root / "build" / "release.map"
        self._write_good_link_map()
        self.app = self.root / "build" / "app.exe"
        self.sdl = self.root / "build" / "SDL3.dll"
        self.artifact = self.root / "build" / "generated-native.dat"
        self.app.write_bytes(b"MZ synthetic clean app")
        self.sdl.write_bytes(b"MZ synthetic SDL runtime")
        self.artifact.write_bytes(b"generated native region")

    def _write_good_link_map(self) -> None:
        self.link_map.write_text(
            " Timestamp is 00000000 (Thu Jan  1 00:00:00 1970)\n"
            " Preferred load address is 0000000140000000\n"
            " Start         Length     Name                   Class\n"
            " 0001:00000000 00000010H .text CODE\n"
            " Address         Publics by Value              Rva+Base       Lib:Object\n"
            " 0001:00000000 shipping_entry 0000000140001000 f app.obj\n",
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def package(self, output: Path):
        return create_release(
            app=self.app,
            sdl=self.sdl,
            artifacts=[("regions/main.native", self.artifact)],
            output=output,
            root=self.root,
            link_map=self.link_map,
            sources=[self.source],
        )

    def test_package_is_measured_relocatable_and_contains_only_explicit_roles(self) -> None:
        package = self.root / "out" / "release"
        manifest = self.package(package)
        self.assertEqual(manifest["status"], "packaged-not-game-ready")
        relocated = self.root / "relocated" / "arbitrary-name"
        relocated.parent.mkdir()
        shutil.copytree(package, relocated)
        receipt = validate_release(relocated)
        self.assertEqual(receipt["status"], "validated")
        config = json.loads((relocated / "policy" / "default-config.json").read_text())
        self.assertEqual(config["schema"], CONFIG_SCHEMA)
        self.assertEqual(config["audio"]["sample_rate_hz"], 44100)
        self.assertEqual(config["video"]["update_rate_hz"], 100)
        self.assertEqual(config["audio"]["underrun"], "count-and-resume-next-batch")
        self.assertEqual(config["input"]["mapping"]["cross"], "south")
        setup = json.loads((relocated / "policy" / "first-run-setup.json").read_text())
        self.assertEqual(setup["mode"], "one-time-first-run")
        self.assertNotIn(str(package), json.dumps(setup))
        launcher = (relocated / "Wipeout3SE.cmd").read_text()
        self.assertIn("%~dp0", launcher)
        self.assertNotIn(str(package), launcher)

    def test_install_creates_durable_cards_and_remains_fail_closed(self) -> None:
        package = self.root / "release"
        self.package(package)
        user_data = self.root / "user-data"
        receipt = initialize_install(package, user_data)
        self.assertFalse(receipt["game_ready"])
        self.assertEqual(receipt["status"], "awaiting-authenticated-media-import")
        for slot in (1, 2):
            card = user_data / "saves" / f"card{slot}.mcd"
            self.assertEqual(card.stat().st_size, 128 * 1024)
        card1 = user_data / "saves" / "card1.mcd"
        card1.write_bytes(b"bad")
        with self.assertRaisesRegex(ReleaseError, "memory card slot 1 is malformed"):
            initialize_install(package, user_data)
        card1.write_bytes(bytes(128 * 1024))
        (user_data / "config.json").write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ReleaseError, "configuration boundary"):
            initialize_install(package, user_data)

    def test_tamper_unmanifested_file_and_forbidden_payload_fail_closed(self) -> None:
        package = self.root / "release"
        self.package(package)
        (package / "bin" / "Wipeout3SE.exe").write_bytes(b"tampered")
        with self.assertRaisesRegex(ReleaseError, "measurement mismatch"):
            validate_release(package)
        (package / "bin" / "Wipeout3SE.exe").write_bytes(b"MZ synthetic clean app")
        (package / "extra.txt").write_text("unlisted", encoding="utf-8")
        with self.assertRaisesRegex(ReleaseError, "unmanifested"):
            validate_release(package)
        with self.assertRaisesRegex(ReleaseError, "forbidden release payload"):
            create_release(
                app=self.app, sdl=self.sdl,
                artifacts=[("retail-bios.dat", self.artifact)],
                output=self.root / "forbidden", root=self.root,
                link_map=self.link_map, sources=[self.source])

    def test_source_and_link_map_gates_are_mandatory(self) -> None:
        bad_source = self.root / "src" / "bad.cpp"
        bad_source.write_text("void dirty_ram_interp();\n", encoding="utf-8")
        with self.assertRaisesRegex(ReleaseError, "isolation gate"):
            create_release(
                app=self.app, sdl=self.sdl, artifacts=[], output=self.root / "bad-source",
                root=self.root, link_map=self.link_map, sources=[bad_source])
        self.link_map.write_text(
            " Timestamp is 00000000\n Preferred load address is 0000000140000000\n"
            " Start Length Name Class\n 0001:00000000 00000010H .text CODE\n"
            " Address Publics by Value Rva+Base Lib:Object\n"
            " 0001:00000000 tcc_compile_string 0000000140001000 f bad.obj\n",
            encoding="utf-8")
        with self.assertRaisesRegex(ReleaseError, "isolation gate"):
            create_release(
                app=self.app, sdl=self.sdl, artifacts=[], output=self.root / "bad-map",
                root=self.root, link_map=self.link_map, sources=[self.source])
        self._write_good_link_map()

    def test_profile_hash_leakage_fails_closed(self) -> None:
        profile = self.root / "profile.json"
        profile.write_text(json.dumps({"bios": {"sha256": hashlib.sha256(
            self.artifact.read_bytes()).hexdigest()}}), encoding="utf-8")
        with self.assertRaisesRegex(ReleaseError, "forbidden media/BIOS/input identity"):
            create_release(
                app=self.app, sdl=self.sdl,
                artifacts=[("regions/main.native", self.artifact)],
                output=self.root / "forbidden-hash", root=self.root,
                link_map=self.link_map, sources=[self.source], leakage_profile=profile)

    def test_deterministic_soak_and_performance_threshold(self) -> None:
        receipt = run_soak(
            [sys.executable, "-c", "print('stable-frame-digest')"],
            iterations=5, timeout_seconds=5.0, max_p95_ms=5000.0,
            receipt_path=self.root / "receipts" / "soak.json")
        self.assertEqual(receipt["status"], "pass")
        self.assertEqual(receipt["iterations"], 5)
        self.assertEqual(json.loads((self.root / "receipts" / "soak.json").read_text()), receipt)
        with self.assertRaisesRegex(ReleaseError, "nondeterministic"):
            run_soak(
                [sys.executable, "-c", "import time; print(time.time_ns())"],
                iterations=3, timeout_seconds=5.0, max_p95_ms=5000.0)

    def test_relocated_launch_records_frontier_and_rejects_stale_upgrade(self) -> None:
        package = self.root / "original-package"
        self.package(package)
        relocated = self.root / "moved" / "game"
        relocated.parent.mkdir()
        shutil.copytree(package, relocated)
        user_data = self.root / "userdata"
        setup_root = user_data / "setups" / "v1"
        artifact_root = setup_root / "artifacts"
        native = artifact_root / "regions" / "main.native"
        native.parent.mkdir(parents=True)
        native.write_bytes(b"local generated BIOS and game regions")
        artifact_hash = hashlib.sha256(native.read_bytes()).hexdigest()
        catalog_input_hash = "1" * 64
        catalog = {"schema": "psxrecomp.clean-aot.catalog.v1", "variants": [{
            "artifact": {"id": "bios-game", "path": "regions/main.native",
                         "sha256": artifact_hash}}]}
        catalog_hash = hashlib.sha256(canonical_json_bytes(catalog)).hexdigest()
        setup = {"schema": "psxrecomp.clean-aot.product-receipt.v1",
                 "status": "setup-complete", "catalog": {"input_sha256": catalog_input_hash}}
        final = {"schema": "psxrecomp.clean-aot.product-finalize-receipt.v1",
                 "status": "catalog-finalized", "catalog_input_sha256": catalog_input_hash,
                 "catalog_sha256": catalog_hash}
        for name, value in (("receipt.json", setup), ("finalize-receipt.json", final),
                            ("catalog.json", catalog)):
            (setup_root / name).write_bytes(canonical_json_bytes(value) + b"\n")
        lock = generation_lock(setup_root / "receipt.json", setup_root / "finalize-receipt.json",
                               setup_root / "catalog.json", artifact_root)
        active = {"schema": ACTIVE_SCHEMA, "status": "active-not-game-ready",
                  "version": "v1", "setup_root": "setups/v1",
                  "generation_lock": lock, "game_ready": False}
        active_path = user_data / "receipts" / "active-setup.json"
        active_path.parent.mkdir(parents=True)
        active_path.write_bytes(canonical_json_bytes(active) + b"\n")
        frontier = {"schema": "clean-aot.bootstrap-receipt.v1", "stage": "bios-slice-stopped",
                    "bios_instructions_executed": 30, "game_ready": False}
        command = [sys.executable, "-c", f"import json;print(json.dumps({frontier!r}))"]
        receipt = launch_and_record(relocated, user_data, command=command)
        self.assertEqual(receipt["frontier"]["bios_instructions_executed"], 30)
        self.assertFalse(receipt["game_ready"])
        native.write_bytes(b"stale replacement")
        with self.assertRaisesRegex(InstallError, "stale"):
            launch_and_record(relocated, user_data, command=command)

    def test_generation_lock_rejects_cross_setup_finalize_upgrade(self) -> None:
        root = self.root / "upgrade"
        root.mkdir()
        artifact = root / "region.native"
        artifact.write_bytes(b"region")
        digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
        catalog = {"schema": "psxrecomp.clean-aot.catalog.v1",
                   "variants": [{"artifact": {"path": "region.native", "sha256": digest}}]}
        (root / "catalog.json").write_bytes(canonical_json_bytes(catalog) + b"\n")
        catalog_hash = hashlib.sha256(canonical_json_bytes(catalog)).hexdigest()
        (root / "setup.json").write_text(json.dumps(
            {"schema": "psxrecomp.clean-aot.product-receipt.v1",
             "status": "setup-complete", "catalog": {"input_sha256": "a" * 64}}))
        (root / "finalize.json").write_text(json.dumps(
            {"schema": "psxrecomp.clean-aot.product-finalize-receipt.v1",
             "status": "catalog-finalized", "catalog_input_sha256": "b" * 64,
             "catalog_sha256": catalog_hash}))
        with self.assertRaisesRegex(InstallError, "stale for this setup"):
            generation_lock(root / "setup.json", root / "finalize.json",
                            root / "catalog.json", root)


if __name__ == "__main__":
    unittest.main()
