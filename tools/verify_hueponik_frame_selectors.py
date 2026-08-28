#!/usr/bin/env python3
"""Verify Hueponik's PAL 25/50/100 selector timing contract.

This is an evidence verifier, not an installer.  It reads the authenticated
stock and ac46a5d Hueponik executables, pins the selector publisher and the
parameterized timing blocks, then evaluates the three normal-rate modes.  No
code-island bytes or patched executable data are proposed for shipping use.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


STOCK_SHA256 = "0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d"
PATCHED_SHA256 = "7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac"
LOAD_ADDRESS = 0x80010000
EXE_HEADER = 0x800

# Exact selector publisher in the resident (2 MiB) menu.  The relocated 8 MiB
# publisher is independently pinned by verify_hueponik_menu_contract.py.
SELECTOR_PUBLISHER = {
    0x8017BB8C: 0x8E0D0008,
    0x8017BB90: 0x3C098018,
    0x8017BB94: 0x2DAC0001,
    0x8017BB98: 0x258C0001,
    0x8017BB9C: 0xAD2CF02C,
    0x8017BBA0: 0x2DAC0002,
    0x8017BBA4: 0x398C0001,
    0x8017BBA8: 0x3C0A8017,
    0x8017BBAC: 0xAD4C5064,
}

# These instruction sequences prove the parameterized formulas used by both
# p=0 selectors and the p=1 selector.  The addresses are Hueponik evidence
# only; the direct port must implement named semantics at stock owner sites.
TIMING_WORDS = {
    # scheduler: rate, p+1 mode, K=(0x8000>>p)<<(rate-1), table index
    0x80174FC4: 0x94A83AF8, 0x80174FC8: 0x8CA35064,
    0x80174FCC: 0xA4A850A4, 0x80174FD0: 0x24610001,
    0x80174FD4: 0xACA13AFC, 0x80174FD8: 0x34018000,
    0x80174FDC: 0x00610806, 0x80174FE0: 0x2508FFFF,
    0x80174FE4: 0x01010804, 0x80174FE8: 0xACA150A8,
    0x801753C4: 0x00030840, 0x801753C8: 0x00280821,
    0x801753CC: 0x00010880, 0x801753D0: 0x00250821,
    0x801753D4: 0x8C2156C4, 0x801753DC: 0xACA150AC,
    # p=0 race delta is raw rate; p=1 is a divide-by-two accumulator
    0x80174A08: 0x1460000E, 0x80174A0C: 0x25010001,
    0x80174A10: 0xACA056F0, 0x80174A14: 0xACA156F4,
    0x80174A44: 0x8CA356F0, 0x80174A4C: 0x00611821,
    0x80174A50: 0x00030843, 0x80174A54: 0xACA156F4,
    0x80174A58: 0x30630001, 0x80174A5C: 0xACA356F0,
    # phase modulus 2<<p and p=0 published-work transform
    0x80175184: 0x8CA85064, 0x80175188: 0x8CA350B4,
    0x8017518C: 0x94A150A4, 0x80175194: 0x150000AB,
    0x8017519C: 0x2C280002, 0x801751A0: 0x38630001,
    0x801751A4: 0x00681824, 0x801751A8: 0x39080001,
    0x801751AC: 0x00681825, 0x801751B4: 0xACA3506C,
    0x801751C4: 0x8CA35064, 0x801751C8: 0x25080001,
    0x801751CC: 0x34040002, 0x801751D0: 0x00642004,
    0x801751D4: 0x8CA350B0, 0x801751D8: 0x2481FFFF,
    0x801751DC: 0x00611824, 0x801751E0: 0xACA050B4,
    0x801751E4: 0x00681821, 0x801751E8: 0x0064082B,
    # p-dependent loop/special constants
    0x80175448: 0x8CA85064, 0x8017544C: 0x94A350A4,
    0x80175454: 0x15000004, 0x80175458: 0x34010004,
    0x8017545C: 0xACA35434, 0x80175464: 0xACA15068,
    0x80175468: 0xACA05434, 0x80175470: 0xACA35068,
    0x80175684: 0x8CA85064, 0x80175688: 0x94A350A4,
    0x8017568C: 0x3401004D, 0x80175690: 0x00082080,
    0x80175694: 0x00240823, 0x80175698: 0xACA15070,
    0x8017569C: 0x00030840, 0x801756A0: 0x2421000A,
    0x801756A4: 0x00082040, 0x801756A8: 0x00240823,
    0x801756B0: 0xACA15430,
    # p=1 enters the native high-rate mixer; p=0 resumes either stock path
    0x80175604: 0x3C018017, 0x80175608: 0x8C215064,
    0x80175610: 0x1420FF2F, 0x80175618: 0x15800004,
    0x80175620: 0x8E633AFC, 0x80175624: 0x0803C579,
    0x8017562C: 0x96030128, 0x80175630: 0x0803C748,
    # p shifts motion and iteration quantities directly
    0x80174A88: 0x8C215064, 0x80174A90: 0x002A5007,
    0x80174B08: 0x8C215064, 0x80174B10: 0x002B5807,
    0x80174B88: 0x8C215064, 0x80174B90: 0x24210007,
    0x801750CC: 0x8C215064, 0x801750D0: 0x26940001,
    0x801750D4: 0x00221004,
}

COEFFICIENT_TABLE = (0x1111, 0x1000, 0x0862, 0x081E)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def word(image: bytes, address: int) -> int:
    offset = EXE_HEADER + address - LOAD_ADDRESS
    return struct.unpack_from("<I", image, offset)[0]


def selector_parameters(selector: int, rate: int = 1) -> dict[str, int | bool]:
    if selector not in (0, 1, 2):
        raise ValueError("selector must be 0, 1, or 2")
    if rate not in (1, 2):
        raise ValueError("rate must be 1 or 2")
    p = int(selector >= 2)
    skip = 2 if selector == 0 else 1
    return {
        "selector": selector,
        "rate": rate,
        "p": p,
        "gSimVblankSkip": skip,
        "host_hz": 50 << p,
        "crtc_multiplier": 1 << p,
        "mode_word": p + 1,
        "scale_k": (0x8000 >> p) << (rate - 1),
        "coefficient": COEFFICIENT_TABLE[2 * p + rate - 1],
        "phase_mod": 2 << p,
        "loop_multiplier": rate if p else 4,
        "fixed_phase": 0 if p else rate,
        "motion_shift": 7 + p,
        "special_multiplier": 77 - 4 * p,
        "special_shift": 2 * rate + 10 - 2 * p,
        "uses_high_rate_mixer": bool(p),
    }


def schedule(selector: int, rate: int, calls: int) -> dict[str, object]:
    params = selector_parameters(selector, rate)
    p = int(params["p"])
    phase = 0
    race_phase = 0
    work: list[int] = []
    race_delta_cs: list[int] = []
    for _ in range(calls):
        if p:
            race_total = race_phase + rate
            race_tick = race_total >> 1
            race_phase = race_total & 1
        else:
            race_tick = rate
            race_phase = 0
        race_delta_cs.append(2 * race_tick)

        total = phase + rate
        wrapped = int(total >= int(params["phase_mod"]))
        phase = total & (int(params["phase_mod"]) - 1)
        if p:
            published = wrapped
        elif rate < 2:
            published = wrapped ^ 1
        else:
            published = 1
        work.append(published)
    return {
        "work": work,
        "race_delta_cs": race_delta_cs,
        "work_count": sum(work),
        "race_centiseconds": sum(race_delta_cs),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--stock-exe", type=Path)
    parser.add_argument("--hue-exe", type=Path)
    parser.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args()
    root = args.root.resolve()
    stock_path = args.stock_exe or root / "disc/SCES_028.45"
    hue_path = args.hue_exe or root / "prepared_disc/SCES_028.45"
    stock = stock_path.read_bytes()
    hue = hue_path.read_bytes()

    failures: list[str] = []
    if digest(stock) != STOCK_SHA256:
        failures.append("stock executable digest mismatch")
    if digest(hue) != PATCHED_SHA256:
        failures.append("Hueponik executable digest mismatch")
    for address, expected in SELECTOR_PUBLISHER.items():
        if word(hue, address) != expected:
            failures.append(f"selector word mismatch at 0x{address:08X}")
    for address, expected in TIMING_WORDS.items():
        if word(hue, address) != expected:
            failures.append(f"timing word mismatch at 0x{address:08X}")
    table = tuple(word(hue, 0x801756C4 + 4 * i) for i in range(4))
    if table != COEFFICIENT_TABLE:
        failures.append("coefficient table mismatch")

    # Normal no-drop cadence: selector 0 observes two VBlanks per scheduler
    # call; selectors 1/2 observe one.  All modes publish 25 temporal-work
    # gates and exactly 100 race centiseconds per wall second.
    normal = ((0, 2, 25), (1, 1, 50), (2, 1, 100))
    mode_results: dict[str, object] = {}
    for selector, rate, calls in normal:
        params = selector_parameters(selector, rate)
        run = schedule(selector, rate, calls)
        if run["work_count"] != 25:
            failures.append(f"selector {selector} work cadence is not 25/s")
        if run["race_centiseconds"] != 100:
            failures.append(f"selector {selector} race clock is not 100 cs/s")
        mode_results[str(selector)] = {"parameters": params, "one_second": run}

    result = {
        "ok": not failures,
        "stock_sha256": digest(stock),
        "hue_sha256": digest(hue),
        "selector_words": len(SELECTOR_PUBLISHER),
        "timing_words": len(TIMING_WORDS),
        "coefficient_table": [f"0x{x:04X}" for x in table],
        "modes": mode_results,
        "failures": failures,
    }
    if args.as_json:
        print(json.dumps(result, sort_keys=True))
    elif failures:
        print("Hueponik frame-selector verification: FAIL")
        for failure in failures:
            print(f"- {failure}")
    else:
        print("Hueponik frame-selector verification: PASS")
        print("- exact 25/50/100 selector publisher and parameterized timing graph")
        print("- selector 0: PAL50 host, skip=2, normal rate=2 -> 25 calls/s")
        print("- selector 1: PAL50 host, skip=1, normal rate=1 -> 50 calls/s")
        print("- selector 2: PAL100 host, skip=1, half-phase -> 100 calls/s")
        print("- every selector: 25 temporal gates/s and 100 race centiseconds/s")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
