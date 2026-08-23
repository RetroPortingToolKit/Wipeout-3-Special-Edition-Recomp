from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "run_fps_benchmark.py"
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location("run_fps_benchmark", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RunFpsBenchmarkTests(unittest.TestCase):
    def test_tasklist_parser_ignores_info_and_malformed_rows(self) -> None:
        output = "\n".join((
            '"Wipeout.exe","4316","Console","1","12,345 K"',
            'INFO: No tasks are running which match the specified criteria.',
            '"broken","not-a-pid"',
            '"Wipeout.exe","99","Console","1","1 K"',
        ))
        self.assertEqual(
            MODULE.parse_tasklist_csv(output),
            [
                {"image": "Wipeout.exe", "pid": 4316},
                {"image": "Wipeout.exe", "pid": 99},
            ],
        )

    def test_command_is_shell_free_and_always_skips_launcher(self) -> None:
        command = MODULE.build_command(
            Path("C:/Build/Game.exe"),
            Path("C:/BIOS/SCPH1001.BIN"),
            Path("C:/Disc/Wip Eout.cue"),
            Path("C:/Runs/fresh saves"),
            "software",
            23995,
            ("--example",),
        )
        self.assertEqual(command[0], str(Path("C:/Build/Game.exe")))
        self.assertEqual(command.count("--no-launcher"), 1)
        self.assertEqual(command.count("--headless"), 1)
        self.assertIn(str(Path("C:/Disc/Wip Eout.cue")), command)
        self.assertEqual(command[-3:], ["--debug-port", "23995", "--example"])

    def test_extra_args_cannot_override_controlled_launch_features(self) -> None:
        for value in (
            "--launcher",
            "--no-headless",
            "--renderer=opengl",
            "--disc",
            "--memcard-dir=C:/old",
        ):
            with self.subTest(value=value), self.assertRaises(MODULE.BenchmarkError):
                MODULE.build_command(
                    Path("game.exe"),
                    Path("bios.bin"),
                    Path("disc.cue"),
                    Path("saves"),
                    "software",
                    None,
                    (value,),
                )

    def test_duration_completion_uses_only_explicit_grace(self) -> None:
        self.assertFalse(MODULE.duration_completed(44.70, 45.0, 0.25))
        self.assertTrue(MODULE.duration_completed(44.75, 45.0, 0.25))
        self.assertTrue(MODULE.duration_completed(45.0, 45.0, 0.0))

    def test_minimum_windows_scales_and_cannot_be_trivial(self) -> None:
        self.assertEqual(MODULE.minimum_telemetry_windows(3.0), 2)
        self.assertEqual(MODULE.minimum_telemetry_windows(45.0), 36)

    def test_runtime_identity_reads_binary_reported_codegen_tag(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "stdout.log"
            log.write_text(
                "psxrecomp: overlay cache audit: "
                "tag=cg11_96b0f5df_gce5e5eff6_f0 "
                "indexed=6 candidates=0 status=ok\n"
                "[CLOCK] title_crtc=PALx2 base=50Hz multiplier=2x "
                "effective=99.493634Hz nominal=100Hz cpu=900% "
                "device_clock=33.8688MHz spu=44100Hz\n",
                encoding="utf-8",
            )
            identity = MODULE.runtime_identity_from_logs(log)
            self.assertEqual(
                identity["cache_audits"][0]["tag"],
                "cg11_96b0f5df_gce5e5eff6_f0",
            )
            self.assertEqual(identity["cache_audits"][0]["indexed"], 6)
            self.assertEqual(
                identity["clock_configurations"][0]["cpu"], "900%"
            )

    def test_cache_identity_changes_with_file_content(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            namespace = root / "cg11_96b0f5df_gce5e5eff6_f0"
            namespace.mkdir()
            library = namespace / "one.dll"
            library.write_bytes(b"one")
            before = MODULE.cache_identity(root)
            library.write_bytes(b"two")
            after = MODULE.cache_identity(root)
            self.assertEqual(before["namespaces"], [namespace.name])
            self.assertNotEqual(before["manifest_sha256"], after["manifest_sha256"])
            self.assertFalse(MODULE.cache_unchanged(before, after))

    def test_cache_identity_is_stable_when_contents_are_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            namespace = root / "cg11_96b0f5df_gce5e5eff6_f0"
            namespace.mkdir()
            (namespace / "one.dll").write_bytes(b"one")
            before = MODULE.cache_identity(root)
            after = MODULE.cache_identity(root)
            self.assertEqual(before, after)
            self.assertTrue(MODULE.cache_unchanged(before, after))

    def test_build_identity_records_timing_run_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "Game.exe"
            executable.write_bytes(b"binary")
            (root / "CMakeCache.txt").write_text(
                "CMAKE_BUILD_TYPE:STRING=Release\n"
                "PSX_GAME_TIMING_RUNS:BOOL=OFF\n",
                encoding="utf-8",
            )
            identity = MODULE.build_identity(executable)
            self.assertEqual(
                identity["cmake_selected"]["PSX_GAME_TIMING_RUNS"], "OFF"
            )


if __name__ == "__main__":
    unittest.main()
