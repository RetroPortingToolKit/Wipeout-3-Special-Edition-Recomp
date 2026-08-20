from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

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

    def write_allowlist(self, payload: dict[str, object]) -> Path:
        path = self.root / "allowlist.json"
        path.write_text(json.dumps(payload), encoding="utf-8", newline="\n")
        return path

    def test_safe_generic_source_passes_and_receipt_is_compact(self) -> None:
        source = self.write_source(
            "clean-aot/region.cpp",
            "uint32_t add(uint32_t left, uint32_t right) { return left + right; }\n",
        )
        first = gate_common.scan_source([source], root=self.root)
        second = gate_common.scan_source([source], root=self.root)

        self.assertEqual(first, second)
        self.assertEqual(first["status"], "pass")
        self.assertEqual(first["summary"]["matches"], 0)
        serialized = gate_common.compact_json(first)
        self.assertNotIn("\n", serialized)
        self.assertNotIn(": ", serialized)
        self.assertEqual(json.loads(serialized), first)
        self.assertEqual(first["files"][0]["path"], "clean-aot/region.cpp")

    def test_source_gate_finds_titles_serials_addresses_and_shipping_symbols(self) -> None:
        source = self.write_source(
            "clean-aot/bad.cpp",
            "// WipEout 3 SE SCES-02845\n"
            "static void *entry = (void *)0x80123456;\n"
            "void run() { dirty_ram_interp(); debug_server_start(); }\n",
        )
        receipt = gate_common.scan_source([source], root=self.root)
        self.assertEqual(receipt["status"], "fail")
        self.assertGreaterEqual(receipt["summary"]["violations"], 5)
        rules = {item["rule"] for item in receipt["findings"]}
        self.assertEqual(rules, {"title_name", "serial", "guest_address", "shipping_symbol"})
        self.assertTrue(all(item["allowed"] is False for item in receipt["findings"]))

    def test_exact_allowlist_is_narrow_and_windows_safe(self) -> None:
        source = self.write_source(
            "clean-aot/policy.cpp",
            "// reviewed adapter\n"
            "constexpr auto base = 0x80010000;\n"
            "const char *name = \"WipEout\";\n",
        )
        allowlist = self.write_allowlist(
            {
                "format": gate_common.ALLOWLIST_FORMAT,
                "source": [
                    {
                        "rule": "guest_address",
                        "path": "clean-aot\\policy.cpp",
                        "line": 2,
                        "match": "0x80010000",
                        "reason": "reviewed policy boundary",
                    },
                    {
                        "rule": "title_name",
                        "path": "clean-aot/policy.cpp",
                        "line": 3,
                        "match": "WipEout",
                        "reason": "policy-only label, outside clean-aot admission",
                    },
                ],
                "link_map": [],
            }
        )
        receipt = gate_common.scan_source([source], root=self.root, allowlist_path=allowlist)
        self.assertEqual(receipt["status"], "pass")
        self.assertEqual(receipt["summary"]["allowlisted"], 2)
        self.assertEqual(receipt["summary"]["violations"], 0)
        self.assertEqual(receipt["allowlist"]["path"], "allowlist.json")
        self.assertEqual(receipt["findings"][0]["path"], "clean-aot/policy.cpp")

    def test_allowlist_rejects_globs_and_traversal(self) -> None:
        for path_value in ("clean-aot/*.cpp", "../outside.cpp", "C:\\outside.cpp"):
            with self.subTest(path=path_value):
                allowlist = self.write_allowlist(
                    {
                        "format": gate_common.ALLOWLIST_FORMAT,
                        "source": [
                            {
                                "rule": "title_name",
                                "path": path_value,
                                "line": 1,
                                "match": "WipEout",
                                "reason": "not a wildcard",
                            }
                        ],
                        "link_map": [],
                    }
                )
                with self.assertRaises(gate_common.GateError):
                    gate_common.load_allowlist(allowlist)

    def test_unallowlisted_match_keeps_source_gate_failed(self) -> None:
        source = self.write_source("clean-aot/mixed.cpp", "auto a = 0x80010000;\nauto b = 0x80100000;\n")
        allowlist = self.write_allowlist(
            {
                "format": gate_common.ALLOWLIST_FORMAT,
                "source": [
                    {
                        "rule": "guest_address",
                        "path": "clean-aot/mixed.cpp",
                        "line": 1,
                        "match": "0x80010000",
                        "reason": "one reviewed boundary",
                    }
                ],
                "link_map": [],
            }
        )
        receipt = gate_common.scan_source([source], root=self.root, allowlist_path=allowlist)
        self.assertEqual(receipt["status"], "fail")
        self.assertEqual(receipt["summary"]["allowlisted"], 1)
        self.assertEqual(receipt["summary"]["violations"], 1)

    def test_link_map_safe_symbols_prove_absence(self) -> None:
        link_map = self.root / "maps" / "release.map"
        link_map.write_text(
            "                 0x00000000                region_entry\n"
            "0001:00000000       region_entry 00401000 f\n",
            encoding="utf-8",
            newline="\n",
        )
        receipt = gate_common.scan_link_map(link_map, root=self.root)
        self.assertEqual(receipt["status"], "pass")
        self.assertTrue(receipt["forbidden_symbols_absent"])
        self.assertEqual(receipt["summary"]["matches"], 0)

    def test_link_map_catches_gnu_nm_and_demangled_forms(self) -> None:
        link_map = self.root / "maps" / "bad.map"
        link_map.write_text(
            " .text._Z18psx_unknown_dispatchv 0x00000000 0x10\n"
            "0001:00000000       debug_server 00401000 f\n"
            " T psx::runtime::dirty_ram_interp()\n"
            " .text._ZN3psx12debug_serverEv 0x00000000 0x10\n",
            encoding="utf-8",
            newline="\n",
        )
        receipt = gate_common.scan_link_map(link_map, root=self.root)
        self.assertEqual(receipt["status"], "fail")
        self.assertFalse(receipt["forbidden_symbols_absent"])
        self.assertEqual(receipt["summary"]["matches"], 4)
        self.assertEqual(
            {item["match"] for item in receipt["findings"]},
            {"psx_unknown_dispatch", "debug_server", "dirty_ram_interp"},
        )

    def test_link_map_allowlist_is_annotation_only_and_cannot_fake_absence(self) -> None:
        link_map = self.root / "maps" / "diagnostic.map"
        link_map.write_text(" T debug_server\n", encoding="utf-8", newline="\n")
        allowlist = self.write_allowlist(
            {
                "format": gate_common.ALLOWLIST_FORMAT,
                "source": [],
                "link_map": [
                    {
                        "symbol": "debug_server",
                        "path": "maps/diagnostic.map",
                        "line": 1,
                        "reason": "diagnostic-only map, never release",
                    }
                ],
            }
        )
        receipt = gate_common.scan_link_map(link_map, root=self.root, allowlist_path=allowlist)
        self.assertEqual(receipt["status"], "fail")
        self.assertFalse(receipt["forbidden_symbols_absent"])
        self.assertEqual(receipt["summary"]["allowlisted"], 1)
        self.assertEqual(receipt["summary"]["unallowlisted"], 0)

    def test_cli_exit_codes_and_receipt_output_are_hermetic(self) -> None:
        source = self.write_source("clean-aot/cli.cpp", "int main() { return 0; }\n")
        output = self.root / "evidence" / "source.json"
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            result = source_main(
                ["--root", str(self.root), "--source", str(source), "--json-out", str(output)]
            )
        self.assertEqual(result, 0)
        self.assertEqual(output.read_text(encoding="utf-8"), stdout.getvalue())
        self.assertEqual(output.read_text(encoding="utf-8").count("\n"), 1)

        link_map = self.root / "maps" / "bad.map"
        link_map.write_text(" T fntrace\n", encoding="utf-8", newline="\n")
        with contextlib.redirect_stdout(io.StringIO()):
            result = link_map_main(["--root", str(self.root), "--link-map", str(link_map)])
        self.assertEqual(result, 1)


if __name__ == "__main__":
    unittest.main()
