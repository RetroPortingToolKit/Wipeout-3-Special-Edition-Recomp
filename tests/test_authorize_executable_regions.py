from __future__ import annotations

import importlib.util
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "tools" / "analysis" / "authorize_executable_regions.py"
SPEC = importlib.util.spec_from_file_location("authorize_executable_regions", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def words(*values: int) -> bytes:
    return b"".join(value.to_bytes(4, "little") for value in values)


def test_direct_closure_and_delay_slot_ownership() -> None:
    base = 0x80010000
    image = words(0x08004003, 0, 0, 0x03E00008, 0)
    report = MODULE.analyze(image, base, {base}, set(range(base, base + len(image), 4)), "test")
    assert report["authorized_instruction_count"] == 4
    assert report["delay_slot_owners"] == [
        {"delay_slot": base + 4, "owner": base},
        {"delay_slot": base + 16, "owner": base + 12},
    ]
    assert report["direct_closure_complete"]


def test_indirect_and_missing_evidence_fail_closed() -> None:
    base = 0x80020000
    image = words(0x00800008, 0, 0)
    report = MODULE.analyze(image, base, {base}, {base, base + 4}, "test")
    assert report["unresolved_indirects"] == [
        {"pc": base, "source_register": 4,
         "category": "argument_callback_target"}
    ]
    assert not report["finite_indirect_closure"]
    assert report["unknown_ranges"] == [{"start": base + 8, "end_exclusive": base + 12}]


def test_control_transfer_in_delay_slot_is_rejected() -> None:
    base = 0x80030000
    image = words(0x0800C002, 0x03E00008, 0)
    report = MODULE.analyze(image, base, {base}, set(range(base, base + len(image), 4)), "test")
    assert report["authorized_instruction_count"] == 0
    assert report["rejected_direct_edges"][0]["reason"] == "control_in_delay_slot"


def test_literal_indirect_target_is_finite() -> None:
    base = 0x80040000
    # lui t0,0x8004; ori t0,t0,0x0010; jr t0; nop; jr ra; nop
    image = words(0x3C088004, 0x35080010, 0x01000008, 0, 0x03E00008, 0)
    allowed = set(range(base, base + len(image), 4))
    report = MODULE.analyze(image, base, {base}, allowed, "test")
    assert report["indirect_targets"] == [
        {"source": base + 8, "target": base + 16,
         "proof": "local_literal_register_construction"}
    ]
    assert report["finite_indirect_closure"]
