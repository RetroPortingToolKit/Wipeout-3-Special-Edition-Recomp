from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.cleanroom import gate_common
from tools.cleanroom.scan_clean_aot import main as source_main
from tools.cleanroom.scan_release_link_map import main as link_map_main


class CleanroomIsolationGateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "clean-aot").mkdir()
        (self.root / "maps").mkdir()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write_source(self, relative: str, text: str) -> Path:
        path = self.root / Path(relative)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
        return path

    def write_allowlist(self, payload: dict[str, object], name: str = "allowlist.json") -> Path:
        path = self.root / name
        path.write_text(json.dumps(payload), encoding="utf-8", newline="\n")
        return path

    def write_link_map(self, dialect: str, body: str, name: str = "release.map") -> Path:
        headers = {
            "gnu-ld": (
                "Memory Configuration\n\n"
                "Linker script and memory map\n\n"
                ".text           0x0000000000401000       0x40\n"
            ),
            "llvm-lld": (
                "Address  Size     Align Out     In      Symbol\n"
                "00401000 00000040    16 .text\n"
            ),
            "msvc-link": (
                " Preferred load address is 0000000140000000\n\n"
                " Start         Length     Name                   Class\n"
                " 0001:00000000 00000040H .text                   CODE\n\n"
                " Address         Publics by Value              Rva+Base       Lib:Object\n"
            ),
            "apple-ld64": (
                "# Path: build/release/app\n"
                "# Arch: x86_64\n"
                "# Object files:\n"
                "# Sections:\n"
                "# Address\tSize\tSegment\tSection\n"
                "0x100001000 0x00000040 __TEXT __text\n"
                "# Symbols:\n"
                "# Address\tSize\tFile\tName\n"
            ),
        }
        path = self.root / "maps" / name
        path.write_text(headers[dialect] + body, encoding="utf-8", newline="\n")
        return path

    @staticmethod
    def allowlist_payload(
        *,
        source: list[dict[str, object]] | None = None,
        link_map: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        return {
            "format": gate_common.ALLOWLIST_FORMAT,
            "source": source or [],
            "link_map": link_map or [],
        }

    def test_safe_generic_source_passes_and_receipt_is_deterministic(self) -> None:
        first_source = self.write_source(
            "clean-aot/zeta/region.cpp",
            "uint32_t add(uint32_t left, uint32_t right) { return left + right; }\n",
        )
        second_source = self.write_source("clean-aot/alpha/CMakeLists.txt", "project(generic)\n")
        first = gate_common.scan_source([first_source, second_source], root=self.root)
        second = gate_common.scan_source([second_source, first_source], root=self.root)

        self.assertEqual(first, second)
        self.assertEqual(first["status"], "pass")
        self.assertEqual(
            first["summary"],
            {"allowlisted": 0, "files": 2, "matches": 0, "violations": 0},
        )
        self.assertEqual(
            [item["path"] for item in first["files"]],
            ["clean-aot/alpha/CMakeLists.txt", "clean-aot/zeta/region.cpp"],
        )
        serialized = gate_common.compact_json(first)
        self.assertNotIn("\n", serialized)
        self.assertNotIn(": ", serialized)
        self.assertEqual(json.loads(serialized), first)

    def test_directory_scan_includes_build_configuration_docs_and_json(self) -> None:
        self.write_source("clean-aot/component/CMakeLists.txt", "project(w3_renderer)\n")
        self.write_source("clean-aot/component/README.md", "generic notes\n")
        self.write_source("clean-aot/component/config.json", '{"catalog":"generic"}\n')
        cache = self.root / "clean-aot" / "component" / "__pycache__"
        cache.mkdir()
        (cache / "generated.pyc").write_bytes(b"\xff\x00generated")
        build = self.root / "clean-aot" / "component" / ".build-test"
        build.mkdir()
        (build / "CMakeCache.txt").write_text("generated build\n", encoding="utf-8")
        (build / "artifact.exe").write_bytes(b"\xff\x00generated")

        receipt = gate_common.scan_source([self.root / "clean-aot"], root=self.root)

        self.assertEqual(receipt["summary"]["files"], 3)
        self.assertEqual(receipt["status"], "fail")
        self.assertEqual(receipt["findings"][0]["rule_id"], "title.w3_alias")

    def test_source_gate_finds_title_aliases_serial_forms_addresses_and_symbols(self) -> None:
        source = self.write_source(
            "clean-aot/bad.cpp",
            "// WipEout 3 SE; w3_race; wo3se; SCES_028.45\n"
            "auto a = 0x8001'0000u; auto b = 0xA001_0000;\n"
            "auto c = 2147549184_u32; auto d = 0x1F800000u32;\n"
            "auto e = 0x1FC00000; auto f = 0xFFFE0130;\n"
            "void run() { dirty_ram_interp(); debug_server_start(); }\n",
        )
        receipt = gate_common.scan_source([source], root=self.root)

        self.assertEqual(receipt["status"], "fail")
        rules = {item["rule"] for item in receipt["findings"]}
        self.assertEqual(rules, {"title_name", "serial", "guest_address", "shipping_symbol"})
        self.assertEqual(
            {item["rule_id"] for item in receipt["findings"] if item["rule"] == "title_name"},
            {"title.wipeout", "title.w3_alias", "title.wo3_alias"},
        )
        classifications = {
            item["classification"]
            for item in receipt["findings"]
            if item["rule"] == "guest_address"
        }
        self.assertEqual(
            classifications,
            {"ram_kseg0", "ram_kseg1", "scratchpad", "bios_physical", "cache_control"},
        )
        self.assertTrue(all(item["allowed"] is False for item in receipt["findings"]))

    def test_source_gate_avoids_near_matches_and_allows_test_oracle_source(self) -> None:
        source = self.write_source(
            "clean-aot/safe.cpp",
            "int row3 = 0; int w30 = 0; // SCES-02846\n"
            "auto host_mask = 0x7FFFFFFF;\n"
            "void runtime_compilerish(); void coracle();\n"
            "void r3000a_semantic_reference_oracle();\n",
        )
        receipt = gate_common.scan_source([source], root=self.root)
        self.assertEqual(receipt["status"], "pass")
        self.assertEqual(receipt["summary"]["matches"], 0)

    def test_source_gate_rejects_empty_tree_binary_and_invalid_utf8(self) -> None:
        with self.assertRaisesRegex(gate_common.GateError, "no regular files"):
            gate_common.scan_source([self.root / "clean-aot"], root=self.root)

        binary = self.root / "clean-aot" / "binary.cpp"
        binary.write_bytes(b"W\x00i\x00p\x00E\x00o\x00u\x00t\x00")
        with self.assertRaisesRegex(gate_common.GateError, "control byte"):
            gate_common.scan_source([binary], root=self.root)

        binary.write_bytes(b"\xff\xfe\x01")
        with self.assertRaisesRegex(gate_common.GateError, "UTF-8 source"):
            gate_common.scan_source([binary], root=self.root)

    def test_root_containment_rejects_external_source_and_allowlist(self) -> None:
        source = self.write_source("clean-aot/safe.cpp", "int safe;\n")
        with tempfile.TemporaryDirectory() as outside_temp:
            outside = Path(outside_temp)
            external_source = outside / "external.cpp"
            external_source.write_text("int external;\n", encoding="utf-8")
            with self.assertRaisesRegex(gate_common.GateError, "outside root"):
                gate_common.scan_source([external_source], root=self.root)

            external_allowlist = outside / "allowlist.json"
            external_allowlist.write_text(
                json.dumps(self.allowlist_payload()), encoding="utf-8", newline="\n"
            )
            with self.assertRaisesRegex(gate_common.GateError, "outside root"):
                gate_common.scan_source(
                    [source], root=self.root, allowlist_path=external_allowlist
                )

    def test_source_symlink_is_rejected_when_supported(self) -> None:
        target = self.write_source("clean-aot/target.cpp", "int target;\n")
        link = self.root / "clean-aot" / "link.cpp"
        try:
            os.symlink(target, link)
        except (NotImplementedError, OSError) as error:
            self.skipTest(f"symlink creation unavailable: {error}")
        with self.assertRaisesRegex(gate_common.GateError, "symlink or junction"):
            gate_common.scan_source([link], root=self.root)

    def test_link_like_guard_fails_closed_without_platform_symlink_privilege(self) -> None:
        source = self.write_source("clean-aot/guard.cpp", "int guard;\n")
        with mock.patch.object(gate_common, "_is_link_like", return_value=True):
            with self.assertRaisesRegex(gate_common.GateError, "symlink or junction"):
                gate_common.scan_source([source], root=self.root)

    def test_exact_allowlist_is_narrow_and_windows_safe(self) -> None:
        source = self.write_source(
            "clean-aot/policy.cpp",
            "// reviewed adapter\n"
            "constexpr auto base = 0x80010000;\n"
            "const char *name = \"WipEout\";\n",
        )
        allowlist = self.write_allowlist(
            self.allowlist_payload(
                source=[
                    {
                        "rule": "guest_address",
                        "path": "clean-aot\\policy.cpp",
                        "line": 2,
                        "match": "0x80010000",
                        "reason": "reviewed architecture boundary",
                    },
                    {
                        "rule": "title_name",
                        "path": "clean-aot/policy.cpp",
                        "line": 3,
                        "match": "WipEout",
                        "reason": "reviewed policy adapter",
                    },
                ]
            )
        )
        receipt = gate_common.scan_source([source], root=self.root, allowlist_path=allowlist)
        self.assertEqual(receipt["status"], "pass")
        self.assertEqual(receipt["summary"]["allowlisted"], 2)
        self.assertEqual(receipt["allowlist"]["path"], "allowlist.json")
        self.assertFalse(Path(receipt["allowlist"]["path"]).is_absolute())

    def test_allowlist_rejects_globs_traversal_drive_relative_and_invalid_types(self) -> None:
        invalid_paths = (
            "clean-aot/*.cpp",
            "../outside.cpp",
            "C:\\outside.cpp",
            "C:drive-relative.cpp",
            "clean-aot/bad\nname.cpp",
            " clean-aot/space.cpp",
        )
        for path_value in invalid_paths:
            with self.subTest(path=path_value):
                allowlist = self.write_allowlist(
                    self.allowlist_payload(
                        source=[
                            {
                                "rule": "title_name",
                                "path": path_value,
                                "line": 1,
                                "match": "WipEout",
                                "reason": "invalid path",
                            }
                        ]
                    )
                )
                with self.assertRaises(gate_common.GateError):
                    gate_common.load_allowlist(allowlist)

        allowlist = self.write_allowlist(
            self.allowlist_payload(
                source=[
                    {
                        "rule": ["title_name"],
                        "path": "clean-aot/file.cpp",
                        "line": 1,
                        "match": "WipEout",
                        "reason": "invalid rule type",
                    }
                ]
            )
        )
        with self.assertRaises(gate_common.GateError):
            gate_common.load_allowlist(allowlist)

    def test_allowlist_rejects_duplicate_selectors_even_when_reasons_differ(self) -> None:
        selector = {
            "rule": "title_name",
            "path": "clean-aot/file.cpp",
            "line": 1,
            "match": "WipEout",
            "reason": "first review",
        }
        duplicate = dict(selector, reason="second review")
        allowlist = self.write_allowlist(
            self.allowlist_payload(source=[selector, duplicate])
        )
        with self.assertRaisesRegex(gate_common.GateError, "duplicate exception selectors"):
            gate_common.load_allowlist(allowlist)

    def test_allowlist_path_and_match_casing_are_exact_on_every_platform(self) -> None:
        source = self.write_source(
            "clean-aot/Policy.cpp", 'const char *name = "WipEout";\n'
        )
        for path, match in (
            ("clean-aot/policy.cpp", "WipEout"),
            ("clean-aot/Policy.cpp", "wipeout"),
        ):
            with self.subTest(path=path, match=match):
                allowlist = self.write_allowlist(
                    self.allowlist_payload(
                        source=[
                            {
                                "rule": "title_name",
                                "path": path,
                                "line": 1,
                                "match": match,
                                "reason": "case mismatch must not broaden an exception",
                            }
                        ]
                    )
                )
                with self.assertRaisesRegex(gate_common.GateError, "unused allowlist source"):
                    gate_common.scan_source(
                        [source], root=self.root, allowlist_path=allowlist
                    )

    def test_allowlist_record_cannot_exempt_multiple_findings_without_columns(self) -> None:
        source = self.write_source(
            "clean-aot/duplicate.cpp",
            "auto a = 0x80010000, b = 0x80010000;\n",
        )
        broad = self.write_allowlist(
            self.allowlist_payload(
                source=[
                    {
                        "rule": "guest_address",
                        "path": "clean-aot/duplicate.cpp",
                        "line": 1,
                        "match": "0x80010000",
                        "reason": "only one finding was reviewed",
                    }
                ]
            )
        )
        with self.assertRaisesRegex(gate_common.GateError, "matches multiple findings"):
            gate_common.scan_source([source], root=self.root, allowlist_path=broad)

        exact = self.write_allowlist(
            self.allowlist_payload(
                source=[
                    {
                        "rule": "guest_address",
                        "path": "clean-aot/duplicate.cpp",
                        "line": 1,
                        "column": 10,
                        "match": "0x80010000",
                        "reason": "first exact occurrence",
                    },
                    {
                        "rule": "guest_address",
                        "path": "clean-aot/duplicate.cpp",
                        "line": 1,
                        "column": 26,
                        "match": "0x80010000",
                        "reason": "second exact occurrence",
                    },
                ]
            ),
            name="exact.json",
        )
        receipt = gate_common.scan_source([source], root=self.root, allowlist_path=exact)
        self.assertEqual(receipt["status"], "pass")
        self.assertEqual(receipt["summary"]["allowlisted"], 2)

    def test_unused_allowlist_record_is_rejected(self) -> None:
        source = self.write_source("clean-aot/safe.cpp", "int safe;\n")
        allowlist = self.write_allowlist(
            self.allowlist_payload(
                source=[
                    {
                        "rule": "title_name",
                        "path": "clean-aot/safe.cpp",
                        "line": 99,
                        "match": "WipEout",
                        "reason": "stale exception",
                    }
                ]
            )
        )
        with self.assertRaisesRegex(gate_common.GateError, "unused allowlist source"):
            gate_common.scan_source([source], root=self.root, allowlist_path=allowlist)

    def test_unallowlisted_match_keeps_source_gate_failed(self) -> None:
        source = self.write_source(
            "clean-aot/mixed.cpp", "auto a = 0x80010000;\nauto b = 0x80100000;\n"
        )
        allowlist = self.write_allowlist(
            self.allowlist_payload(
                source=[
                    {
                        "rule": "guest_address",
                        "path": "clean-aot/mixed.cpp",
                        "line": 1,
                        "match": "0x80010000",
                        "reason": "one reviewed boundary",
                    }
                ]
            )
        )
        receipt = gate_common.scan_source([source], root=self.root, allowlist_path=allowlist)
        self.assertEqual(receipt["status"], "fail")
        self.assertEqual(receipt["summary"]["allowlisted"], 1)
        self.assertEqual(receipt["summary"]["violations"], 1)

    def test_recognized_link_map_dialects_pass_and_are_identified(self) -> None:
        for dialect in ("gnu-ld", "llvm-lld", "msvc-link", "apple-ld64"):
            with self.subTest(dialect=dialect):
                link_map = self.write_link_map(
                    dialect,
                    " 0001:00000000       region_entry 00401000 f\n",
                    name=f"{dialect}.map",
                )
                first = gate_common.scan_link_map(link_map, root=self.root)
                second = gate_common.scan_link_map(link_map, root=self.root)
                self.assertEqual(first, second)
                self.assertEqual(first["status"], "pass")
                self.assertTrue(first["forbidden_symbols_absent"])
                self.assertEqual(first["link_map"]["dialect"], dialect)

    def test_link_map_catches_gnu_llvm_and_msvc_symbol_forms(self) -> None:
        cases = (
            ("gnu-ld", " .text._Z20psx_unknown_dispatchv 0x401000 0x10\n", "psx_unknown_dispatch"),
            (
                "llvm-lld",
                " 401000 401000 10 16 .text app.o psx::runtime::dirty_ram_interp()\n",
                "dirty_ram_interp",
            ),
            (
                "msvc-link",
                " 0001:00000000 ?debug_server@@YAXXZ 0000000140001000 app.obj\n",
                "debug_server",
            ),
        )
        for dialect, body, expected in cases:
            with self.subTest(dialect=dialect):
                link_map = self.write_link_map(dialect, body, name=f"bad-{dialect}.map")
                receipt = gate_common.scan_link_map(link_map, root=self.root)
                self.assertEqual(receipt["status"], "fail")
                self.assertFalse(receipt["forbidden_symbols_absent"])
                self.assertIn(expected, {item["match"] for item in receipt["findings"]})

    def test_link_map_blocks_offline_compiler_oracle_tcc_and_compatibility_dispatch(self) -> None:
        link_map = self.write_link_map(
            "gnu-ld",
            " CMakeFiles/clean_aot_compiler.dir/emitter.cpp.o\n"
            " T r3000a_semantic_reference_oracle\n"
            " U tcc_new\n"
            " U tcc_compile_string\n"
            " T compatibility_dispatch\n",
        )
        receipt = gate_common.scan_link_map(link_map, root=self.root)
        self.assertEqual(receipt["status"], "fail")
        self.assertEqual(
            {item["match"] for item in receipt["findings"]},
            {
                "clean_aot_compiler",
                "r3000a_semantic_reference_oracle",
                "tcc_new",
                "tcc_compile_string",
                "compatibility_dispatch",
            },
        )

    def test_link_map_rejects_empty_arbitrary_binary_and_external_inputs(self) -> None:
        for name, payload in (
            ("empty.map", b""),
            ("arbitrary.map", b"this is not a linker map\n"),
            ("headers-only.map", b"Memory Configuration\nLinker script and memory map\n"),
            ("binary.map", b"Memory Configuration\x00Linker script and memory map"),
            ("invalid-utf8.map", b"\xff\xfe"),
        ):
            with self.subTest(name=name):
                path = self.root / "maps" / name
                path.write_bytes(payload)
                with self.assertRaises(gate_common.GateError):
                    gate_common.scan_link_map(path, root=self.root)

        with tempfile.TemporaryDirectory() as outside_temp:
            outside = Path(outside_temp) / "release.map"
            outside.write_text(
                "Memory Configuration\nLinker script and memory map\n.text 0x401000 0x10\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(gate_common.GateError, "outside root"):
                gate_common.scan_link_map(outside, root=self.root)

    def test_link_map_allowlist_is_annotation_only_and_cannot_fake_absence(self) -> None:
        link_map = self.write_link_map("gnu-ld", " T debug_server\n")
        lines = link_map.read_text(encoding="utf-8").splitlines()
        line_number = lines.index(" T debug_server") + 1
        allowlist = self.write_allowlist(
            self.allowlist_payload(
                link_map=[
                    {
                        "symbol": "debug_server",
                        "path": "maps/release.map",
                        "line": line_number,
                        "reason": "diagnostic-only map, never release",
                    }
                ]
            )
        )
        receipt = gate_common.scan_link_map(link_map, root=self.root, allowlist_path=allowlist)
        self.assertEqual(receipt["status"], "fail")
        self.assertFalse(receipt["forbidden_symbols_absent"])
        self.assertEqual(receipt["summary"]["allowlisted"], 1)
        self.assertEqual(receipt["summary"]["unallowlisted"], 0)

    def test_cli_exit_codes_and_exclusive_receipt_output(self) -> None:
        safe_source = self.write_source("clean-aot/cli.cpp", "int main() { return 0; }\n")
        bad_source = self.write_source("clean-aot/bad-cli.cpp", "void w3_race();\n")
        binary_source = self.root / "clean-aot" / "binary-cli.cpp"
        binary_source.write_bytes(b"bad\x00source")
        output = self.root / "evidence" / "source.json"

        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            result = source_main(
                ["--root", str(self.root), "--source", str(safe_source), "--json-out", str(output)]
            )
        self.assertEqual(result, 0)
        self.assertEqual(output.read_text(encoding="utf-8"), stdout.getvalue())
        self.assertEqual(output.read_text(encoding="utf-8").count("\n"), 1)

        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(
                source_main(["--root", str(self.root), "--source", str(bad_source)]),
                1,
            )
            self.assertEqual(
                source_main(["--root", str(self.root), "--source", str(binary_source)]),
                2,
            )
            self.assertEqual(
                source_main(
                    [
                        "--root",
                        str(self.root),
                        "--source",
                        str(safe_source),
                        "--json-out",
                        str(output),
                    ]
                ),
                2,
            )

        safe_map = self.write_link_map(
            "msvc-link", " 0001:00000000 region_entry 140001000 app.obj\n"
        )
        bad_map = self.write_link_map(
            "llvm-lld",
            " 401000 401000 10 16 .text app.o fntrace\n",
            "bad.map",
        )
        malformed_map = self.root / "maps" / "malformed.map"
        malformed_map.write_text("not a map\n", encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(
                link_map_main(["--root", str(self.root), "--link-map", str(safe_map)]),
                0,
            )
            self.assertEqual(
                link_map_main(["--root", str(self.root), "--link-map", str(bad_map)]),
                1,
            )
            self.assertEqual(
                link_map_main(["--root", str(self.root), "--link-map", str(malformed_map)]),
                2,
            )


if __name__ == "__main__":
    unittest.main()
