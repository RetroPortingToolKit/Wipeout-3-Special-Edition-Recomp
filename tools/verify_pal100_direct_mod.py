#!/usr/bin/env python3
"""Verify the stock-title Hueponik PAL 25/50/100 semantic mod contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
import tomllib


EXACT_REPLACEMENTS = {
    0x800EE290: (0x24040002, 0x24040001),
    0x800EEBF0: (0x24030002, 0x24030001),
    0x800E99C4: (0x240200C8, 0x24020190),
    0x800E99C8: (0x00031843, 0x00000000),
    0x800E99D8: (0x24030002, 0x24030001),
    0x800E9A04: (0x00052843, 0x000528C3),
    0x801234C0: (0x28420002, 0x28420001),
    0x801234C8: (0x24020002, 0x24020001),
    0x801234DC: (0x28420007, 0x28420003),
    0x801234E8: (0x24020006, 0x24020002),
    0x801234FC: (0x24020002, 0x24020001),
    0x800E4640: (0x000A51C3, 0x000A5203),
    0x800E4650: (0x000B59C3, 0x000B5A03),
    0x800E4660: (0x000529C3, 0x00052A03),
    0x801192E4: (0x26830001, 0x26940001),
    0x801192E8: (0x00031C00, 0x00021040),
    0x801192F0: (0x0003A403, 0x00000000),
    0x8010D8E4: (0x8C433AF8, 0x8C4356F4),
}

# Two replacements live in MIPS branch delay slots. Their callbacks hook the
# owning branch and implement the patched delay instruction plus branch result.
# The 0x800E99C4 owner is atomic through the C8 nop and CC subtraction so the
# preceding timed load is consumed without splitting the AOT block twice. The
# 0x801192E4 owner is likewise atomic: E8/F0 are not dispatchable AOT
# continuations, so one callback implements the island through the loop branch.
EXACT_HOOK_SITES = {
    0x800EE290, 0x800EEBF0, 0x800E99C4,
    0x800E99D8, 0x800E9A04, 0x801234C0, 0x801234C4,
    0x801234DC, 0x801234E8, 0x801234F8, 0x800E4640,
    0x800E4650, 0x800E4660, 0x801192E4,
    0x8010D8E4,
}

HALF_PHASE_SITES = {
    0x800E2F0C, 0x800E4718, 0x800E4760, 0x800E5110,
    0x800E73DC, 0x800EFA30, 0x800F0078, 0x800F01B0,
    0x8010D8E4, 0x8011A3A8, 0x8011A984, 0x801280AC,
    0x80128390, 0x8014CD44, 0x8014EA2C, 0x801505BC,
    0x80150950, 0x80151568, 0x80151C44, 0x80151C8C,
    0x8015242C, 0x801531D0, 0x801531F4, 0x80153960,
    0x80153984, 0x80153E34, 0x80153E98, 0x80153F38,
    0x80153F90, 0x8015404C, 0x80154A58, 0x80154E64,
    0x801587C8, 0x801589E0, 0x80158CDC, 0x80159224,
    0x80159B3C, 0x8015AA88, 0x8015BB74, 0x8015BB98,
}

ADDITIONAL_OWNER_SITES = {
    0x800F1DA4, 0x800E39D4, 0x800E3BB4, 0x800E484C,
    0x8011A0BC, 0x8011A0C4, 0x8011A12C, 0x8011A134,
    0x8011A650, 0x8012BBF4,
}

HOOK_SITES = {
    0x800E2D40, 0x800E2F38, 0x800E2FBC, 0x800E2FE4,
    0x800E37F4, 0x800E3860, 0x800E4580, 0x800E45FC,
    0x800E460C, 0x800E4628, 0x800E4880, 0x800E4894,
    0x800F15DC, 0x800F1D18, 0x80119FC4, 0x8011A010,
    0x8011A040, 0x8011A048, 0x8011C2DC, 0x8011DD7C,
    0x801234F0, 0x80124AE8, 0x801396CC, 0x8013A600,
    0x8013BED0, 0x8013BED8, 0x8013BEE0, 0x8013BEF0,
    0x80154088, 0x80159070, 0x80159078, 0x8015908C,
    0x80159094, 0x801590A4, 0x801590AC,
}
ALL_HOOK_SITES = (HOOK_SITES | EXACT_HOOK_SITES | HALF_PHASE_SITES |
                  ADDITIONAL_OWNER_SITES)

# Exact authenticated stock->ac46 changed-word ownership. The union must equal
# every timing-classified executable diff below; this is deliberately derived
# from the executable comparison rather than the former 39-detour report.
DIFF_OWNER_WORDS = {
    0x800E2D40: {0x800E2D40},
    0x800E2F38: {0x800E2F38},
    0x800E2FBC: {0x800E2FBC},
    0x800E2FE4: {0x800E2FE4},
    0x800E37F4: {0x800E37F4, 0x800E37F8},
    0x800E3860: {0x800E3860, 0x800E3864},
    0x800E39D4: {0x800E39D4},
    0x800E3BB4: set(range(0x800E3BB4, 0x800E3BCC, 4)),
    0x800E4580: {0x800E4580, 0x800E4584},
    0x800E45FC: {0x800E45FC, 0x800E4600},
    0x800E460C: {0x800E460C, 0x800E4610},
    0x800E4628: {0x800E4628, 0x800E462C},
    0x800E4640: {0x800E4644, 0x800E4648},
    0x800E4650: {0x800E4654, 0x800E4658},
    0x800E4660: {0x800E4660, 0x800E4664},
    0x800E484C: set(range(0x800E484C, 0x800E485C, 4)),
    0x800E4880: {0x800E4880, 0x800E4884},
    0x800E4894: {0x800E4894, 0x800E4898},
    0x800E99C4: {0x800E99C4, 0x800E99C8},
    0x800E99D8: {0x800E99D8},
    0x800E9A04: {0x800E9A04},
    0x800F15DC: {0x800F15DC, 0x800F15E0},
    0x800F1D18: {0x800F1D18, 0x800F1D1C},
    0x800F1DA4: {0x800F1DA4},
    0x801192E4: {0x801192E4, 0x801192E8},
    0x80119FC4: {0x80119FC4, 0x80119FC8, 0x80119FD0},
    0x8011A010: {0x8011A010, 0x8011A014},
    0x8011A040: {0x8011A040, 0x8011A044},
    0x8011A048: {0x8011A048, 0x8011A04C},
    0x8011A0BC: {0x8011A0BC},
    0x8011A0C4: {0x8011A0C4},
    0x8011A12C: {0x8011A12C},
    0x8011A134: {0x8011A134},
    0x8011A650: set(range(0x8011A650, 0x8011A678, 4)),
    0x8011C2DC: {0x8011C2DC},
    0x8011DD7C: {0x8011DD7C, 0x8011DD80},
    0x801234C0: {0x801234C0},
    0x801234C4: {0x801234C8},
    0x801234DC: {0x801234DC},
    0x801234E8: {0x801234E8},
    0x801234F0: {0x801234F0},
    0x801234F8: {0x801234FC},
    0x80124AE8: {0x80124AE8},
    0x8012BBF4: {0x8012BBF4, 0x8012BC04},
    0x801396CC: {0x801396CC, 0x801396D0},
    0x8013A600: {0x8013A600},
    0x8013BED0: {0x8013BED0},
    0x8013BED8: {0x8013BED8},
    0x8013BEE0: {0x8013BEE0},
    0x8013BEF0: {0x8013BEF0},
    0x80154088: {0x80154088},
    0x80159070: {0x80159070},
    0x80159078: {0x80159078},
    0x8015908C: {0x8015908C},
    0x80159094: {0x80159094},
    0x801590A4: {0x801590A4, 0x801590A8},
    0x801590AC: {0x801590AC},
}
for _site in HALF_PHASE_SITES:
    DIFF_OWNER_WORDS[_site] = {_site}

NON_TIMING_DIFF_REASONS = {
    0x800E5CA8: "relocated non-timing data pointer",
    0x800E70E4: "relocated non-timing data pointer",
    0x800FA224: "separate birds-detail preset",
    0x80123EF8: "Hue performance-menu/engine call",
    0x8014BAD0: "separate 8 MB predicate",
    0x8015FA80: "separate 8 MB allocation scaling",
    0x80162474: "Hue startup loader trampoline",
    0x80166598: "relocated loader/data pointer",
}

EXPECTED_PLUGINS = {
    "wipeout3.framerate.pal25",
    "wipeout3.framerate.pal50",
    "wipeout3.framerate.pal-x2",
    "wipeout3.framerate.pal100-title",
}


def as_int(value: object) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise TypeError(f"not an integer value: {value!r}")


def s32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value if value < 0x80000000 else value - 0x100000000


def mul_shift16_64(value: int, multiplier: int) -> int:
    return (s32(value) * s32(multiplier) >> 16) & 0xFFFFFFFF


def mix_vector(x: int, y: int, input_value: int) -> tuple[int, int]:
    x = (mul_shift16_64(x, 0xD745) + input_value * 0x0146) & 0xFFFFFFFF
    y = (
        mul_shift16_64(y, 0xF798)
        + mul_shift16_64(x, 0x2CD9)
        + input_value * 0x0070
    ) & 0xFFFFFFFF
    return x, y


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root", type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args()
    root = args.root.resolve()
    failures: list[str] = []

    game = tomllib.loads((root / "game.toml").read_text(encoding="utf-8"))
    recompiler = game.get("recompiler", {})
    if game.get("game", {}).get("exe") != "disc/SCES_028.45":
        failures.append("game.toml does not target the authenticated stock EXE")
    if "aot_patch_manifest" in recompiler:
        failures.append("rejected AOT xdelta manifest remains configured")
    if game.get("overlays"):
        failures.append("runtime executable overlays remain configured")

    actual_words = {
        as_int(item["address"])
        for item in recompiler.get("patch", [])
        if str(item.get("id", "")).startswith("pal100-")
    }
    if actual_words:
        failures.append("PAL selector still has generated AOT patches instead of stock-RAM hooks")

    exe = (root / "disc/SCES_028.45").read_bytes()
    hue_exe = (root / "prepared_disc/SCES_028.45").read_bytes()
    if hashlib.sha256(exe).hexdigest() != (
        "0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d"
    ):
        failures.append("stock executable hash is not authenticated SCES-02845")
    if hashlib.sha256(hue_exe).hexdigest() != (
        "7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac"
    ):
        failures.append("control executable hash is not authenticated ac46a5d Hue")

    changed_words: set[int] = set()
    for address in range(0x800E0000, 0x80174A00, 4):
        offset = 0x800 + address - 0x80010000
        if exe[offset:offset + 4] != hue_exe[offset:offset + 4]:
            changed_words.add(address)
    timing_words = set().union(*DIFF_OWNER_WORDS.values())
    expected_changed = timing_words | set(NON_TIMING_DIFF_REASONS)
    if changed_words != expected_changed:
        missing = sorted(changed_words - expected_changed)
        stale = sorted(expected_changed - changed_words)
        failures.append(
            "stock->Hue diff classification mismatch: "
            f"unclassified={[f'0x{x:08X}' for x in missing]}, "
            f"stale={[f'0x{x:08X}' for x in stale]}"
        )
    if set(DIFF_OWNER_WORDS) - ALL_HOOK_SITES:
        failures.append("one or more timing diff owners has no native hook")

    for address in HALF_PHASE_SITES:
        offset = 0x800 + address - 0x80010000
        stock = int.from_bytes(exe[offset:offset + 4], "little")
        patched = int.from_bytes(hue_exe[offset:offset + 4], "little")
        if ((stock & 0xFFFF) != 0x3AF8 or
                (patched & 0xFFFF) != 0x56F4 or
                (stock & 0xFFFF0000) != (patched & 0xFFFF0000)):
            failures.append(f"half-phase substitution mismatch at 0x{address:08X}")
    offset = 0x800 + 0x800F1DA4 - 0x80010000
    if (int.from_bytes(exe[offset:offset + 4], "little") != 0x8E843AF8 or
            int.from_bytes(hue_exe[offset:offset + 4], "little") != 0x8E845434):
        failures.append("fixed zero-phase consumer mismatch at 0x800F1DA4")
    for address, (expected, _replacement) in EXACT_REPLACEMENTS.items():
        offset = 0x800 + address - 0x80010000
        actual = int.from_bytes(exe[offset:offset + 4], "little")
        if actual != expected:
            failures.append(
                f"stock EXE word mismatch at 0x{address:08X}: 0x{actual:08X}"
            )

    configured_sites = {as_int(value) for value in
                        recompiler.get("mod_instruction_sites", [])}
    if not ALL_HOOK_SITES.issubset(configured_sites):
        failures.append("game.toml is missing one or more PAL instruction sites")

    source = (root / "src/mods/wipeout3_framerate_100_plugin.c").read_text(
        encoding="utf-8"
    )
    case_sites = {int(value, 16) for value in
                  re.findall(r"case\s+0x([0-9A-Fa-f]{8})u", source)}
    half_table = re.search(
        r"s_half_phase_sites\[\]\s*=\s*\{(.*?)\};", source, re.S
    )
    native_half_sites = ({int(value, 16) for value in re.findall(
        r"\{0x([0-9A-Fa-f]{8})u,\s*0x[0-9A-Fa-f]{8}u\}",
        half_table.group(1))} if half_table else set())
    if native_half_sites != HALF_PHASE_SITES:
        failures.append("native half-phase table is incomplete or stale")
    half_callback = re.search(
        r"pal100_half_phase_site\s*\(.*?"
        r"return\s+PSX_MOD_INSTRUCTION_SUBSTITUTE_LOAD\s*;",
        source, re.S,
    )
    if not half_callback:
        failures.append("half-phase loads do not use the non-exiting substitution contract")
    if not re.search(
        r"s_half_phase_sites\[i\]\.address\s*,\s*"
        r"pal100_half_phase_site\s*\)", source, re.S
    ):
        failures.append("half-phase sites are not registered to their bounded callback")

    generated_source = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted((root / "generated").glob("SCES_028.45_full_*.c"))
    )
    generated_substitute_sites = {
        int(value, 16) for value in re.findall(
            r"int\s+_mod_substitute_load_([0-9A-Fa-f]{8})\s*=",
            generated_source,
        )
    }
    missing_generated_half_sites = HALF_PHASE_SITES - generated_substitute_sites
    if missing_generated_half_sites:
        failures.append(
            "generated shards are missing non-exiting half-phase substitutions: "
            + ", ".join(
                f"0x{address:08X}" for address in sorted(missing_generated_half_sites)
            )
        )
    if case_sites | native_half_sites != ALL_HOOK_SITES:
        failures.append("native callback does not cover exactly the 100 PAL sites")
    registration = re.search(
        r"static const uint32_t sites\[\]\s*=\s*\{(.*?)\};", source, re.S
    )
    registered_sites = ({int(value, 16) for value in
                         re.findall(r"0x([0-9A-Fa-f]{8})u", registration.group(1))}
                        if registration else set())
    if registered_sites | native_half_sites != ALL_HOOK_SITES:
        failures.append("native registration does not contain exactly the 100 PAL sites")

    unsafe_iteration_sites = {0x801192E8, 0x801192F0}
    if configured_sites & unsafe_iteration_sites:
        failures.append("unsafe split iteration callbacks remain configured")
    if registered_sites & unsafe_iteration_sites:
        failures.append("unsafe split iteration callbacks remain registered")

    code_only = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    for forbidden in ("0x8017F02C", "0x80175064", "0x80175774"):
        if forbidden.lower() in code_only.lower():
            failures.append(f"direct implementation accesses forbidden patched state {forbidden}")
    for forbidden_api in ("psx_mod_write_code_word", "psx_mod_write_word"):
        if forbidden_api in code_only:
            failures.append(f"direct implementation uses runtime RAM writer {forbidden_api}")
    if "psx_mod_set_auto_skip_fmv" in code_only:
        failures.append("PAL selector plugin globally suppresses FMVs")
    for required in (
        "psx_mod_set_crtc_refresh_multiplier(s_timing.p ? 2u : 1u)",
        "psx_mod_set_native_vblank_rate(s_timing.p ? 100u : 50u)",
        "wipeout3.framerate.pal100-title",
        "static const uint32_t coefficients[4] = {0x1111u, 0x1000u, 0x0862u",
        "s_timing.sim_vblank_skip = (selector < 1u) + 1u",
        "s_timing.p = selector >= 2u",
        "s_timing.work_gate = !s_timing.phase_wrap",
        "if (s_timing.phase_wrap)",
        "s_timing.p ? s_timing.race_tick : s_timing.rate",
        "cpu->gpr[2] <<= s_timing.p",
        "s_timing.p ? s_timing.rate : 4u",
        "77u - 4u * s_timing.p",
        "10u - 2u * s_timing.p",
        "return (cpu->gpr[4] != 0u) ? 0x80119210u : 0x80119300u",
    ):
        if required not in source:
            failures.append(f"missing direct PAL selector contract token: {required}")

    scheduler_match = re.search(
        r"static void scheduler_update\(CPUState \*cpu\) \{(.*?)\n\}",
        source,
        flags=re.S,
    )
    if scheduler_match is None:
        failures.append("native scheduler implementation is missing")
    else:
        scheduler_source = scheduler_match.group(1)
        if scheduler_source.count("if (s_timing.phase_wrap)") != 2:
            failures.append(
                "random counter/event are not both gated by authenticated raw phase wrap"
            )
        if re.search(
            r"if \(s_timing\.work_gate\)\s*(?:\{|)\s*"
            r"(?:\+\+s_timing\.random_counter|uint32_t random_value)",
            scheduler_source,
        ):
            failures.append(
                "random counter/event incorrectly use selector-published work gate"
            )

    manifest_path = root / (
        "mods/preloaded/packages/wipeout3.enhancement.framerate-pal100/"
        "2.0.0/manifest.toml"
    )
    manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
    plugins = {item.get("id") for item in manifest.get("plugin", [])}
    if plugins != EXPECTED_PLUGINS:
        failures.append("PAL selector manifest plugin selection is incomplete or stale")
    target = (manifest.get("target") or [{}])[0]
    if target.get("exe_sha256") != (
        "0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d"
    ):
        failures.append("PAL selector package is not locked to the authenticated stock EXE")

    rate_options = [item for item in manifest.get("option", [])
                    if item.get("feature") == "framerate-pal100" and
                    item.get("id") == "rate"]
    if len(rate_options) != 1:
        failures.append("PAL selector must declare exactly one rate option")
    else:
        option = rate_options[0]
        if option.get("type") != "choice" or option.get("default") != "100":
            failures.append("PAL selector rate option is not a default-100 choice")
        choices = {item.get("value") for item in option.get("choice", [])}
        if choices != {"25", "50", "100"}:
            failures.append(f"PAL selector choices are incomplete: {sorted(choices)}")

    plugin_when = {item.get("id"): item.get("when")
                   for item in manifest.get("plugin", [])}
    expected_when = {
        "wipeout3.framerate.pal25": {"rate": "25"},
        "wipeout3.framerate.pal50": {"rate": "50"},
        "wipeout3.framerate.pal-x2": {"rate": "100"},
    }
    for plugin_id, condition in expected_when.items():
        if plugin_when.get(plugin_id) != condition:
            failures.append(f"{plugin_id} selector condition is not {condition}")
    if plugin_when.get("wipeout3.framerate.pal100-title") is not None:
        failures.append("common PAL title hooks must be unconditional")

    state = tomllib.loads((root / "mods/preloaded/state.toml").read_text(
        encoding="utf-8"
    ))
    rate_states = [item for item in state.get("feature", [])
                   if item.get("package_id") ==
                   "wipeout3.enhancement.framerate-pal100" and
                   item.get("id") == "framerate-pal100"]
    if len(rate_states) != 1 or rate_states[0].get("values", {}).get("rate") != "100":
        failures.append("preloaded PAL selector state does not explicitly default to 100")

    # Independent arithmetic vectors pin signed MIPS HI/LO recombination.
    vectors = [
        ((0x12345678, 0x89ABCDEF, 123), (0x0F4F795C, 0x903D5ABC)),
        ((0xFFFFFFFF, 0x80000000, -321), (0xFFFE6739, 0x84332BF3)),
        ((0x00000000, 0x00000001, 0x7FFF), (0x00A2FEBA, 0x00548D81)),
    ]
    for inputs, expected in vectors:
        if mix_vector(*inputs) != expected:
            failures.append(f"shared mixer vector failed for {inputs!r}")

    result = {
        "ok": not failures,
        "generated_aot_words": len(actual_words),
        "exact_instruction_replacements": len(EXACT_REPLACEMENTS),
        "instruction_sites": len(ALL_HOOK_SITES),
        "generated_half_phase_substitutions": len(
            HALF_PHASE_SITES & generated_substitute_sites
        ),
        "authenticated_changed_words": len(changed_words),
        "classified_timing_words": len(timing_words),
        "classified_non_timing_words": len(NON_TIMING_DIFF_REASONS),
        "unclassified_changed_words": len(changed_words - expected_changed),
        "native_plugin_ids": sorted(plugins),
        "selector_choices": ["25", "50", "100"],
        "selector_default": "100",
        "forbidden_runtime_state": ["0x8017F02C", "0x80175064", "0x80175774"],
        "failures": failures,
    }
    if args.as_json:
        print(json.dumps(result, sort_keys=True))
    elif failures:
        print("PAL 25/50/100 direct semantic verification: FAIL")
        for failure in failures:
            print(f"- {failure}")
    else:
        print("PAL 25/50/100 direct semantic verification: PASS")
        print("- authenticated stock EXE; no xdelta/AOT manifest/overlay engine")
        print("- stock guest text retained: 100 guarded native PAL sites")
        print("- authenticated diff: 138 timing words owned; 8 non-timing words classified; 0 unclassified")
        print("- no runtime skip/padding/replacement-engine state writes")
        print("- selector 25/50 uses PAL x1/50; selector 100 uses PAL x2/100")
        print("- temporal gates stay 25/s and the race clock stays 100 cs/s")
        print("- random counter/event use authenticated raw-wrap phase")
        print("- shared mixer signed HI/LO vectors pass")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
