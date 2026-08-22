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
    run_soak,
    validate_release,
)


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
        setup = json.loads((relocated / "policy" / "first-run-setup.json").read_text())
        self.assertEqual(setup["mode"], "one-time-first-run")
        self.assertNotIn(str(package), json.dumps(setup))

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
            iterations=5, timeout_seconds=5.0, max_p95_ms=5000.0)
        self.assertEqual(receipt["status"], "pass")
        self.assertEqual(receipt["iterations"], 5)
        with self.assertRaisesRegex(ReleaseError, "nondeterministic"):
            run_soak(
                [sys.executable, "-c", "import time; print(time.time_ns())"],
                iterations=3, timeout_seconds=5.0, max_p95_ms=5000.0)


if __name__ == "__main__":
    unittest.main()
