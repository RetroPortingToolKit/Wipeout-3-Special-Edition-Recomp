import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("verify", ROOT / "tools/verify_8mb_runtime_receipt.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


def receipt():
    return {
        "pass": True,
        "live_ram_bytes": 8388608,
        "fallback_detected": False,
        "initialization_order": ["ram_activation", "memory_init", "bios_init", "game_init"],
        "high_address_probe": {"address": 0x807FFFFC, "write_pass": True, "read_pass": True, "value_verified": True},
        "dma_probe": {"pass": True, "high_address_used": True},
        "savestate_metadata": {"live_ram_bytes": 8388608, "ram_section_bytes": 8388608, "ram_size_match": True},
        "pairing": {"ram_bytes": 8388608, "overclock_percent": 900, "pass": True},
    }


def test_valid_receipt():
    assert verify.validate(receipt()) == []


def test_rejects_silent_two_mb_fallback():
    candidate = receipt()
    candidate["live_ram_bytes"] = 2 * 1024 * 1024
    candidate["fallback_detected"] = True
    assert verify.validate(candidate)


def test_rejects_activation_after_bios():
    candidate = receipt()
    candidate["initialization_order"] = ["memory_init", "bios_init", "ram_activation", "game_init"]
    assert any("activation" in error for error in verify.validate(candidate))


def test_rejects_missing_high_ram_dma_and_state_proof():
    candidate = receipt()
    candidate["high_address_probe"]["address"] = 0x801FFFFC
    candidate["dma_probe"]["high_address_used"] = False
    candidate["savestate_metadata"]["ram_section_bytes"] = 2 * 1024 * 1024
    assert len(verify.validate(candidate)) >= 3
