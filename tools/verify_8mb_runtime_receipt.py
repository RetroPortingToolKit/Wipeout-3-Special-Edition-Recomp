"""Fail-closed verifier for the live 8 MB runtime receipt.

This checks evidence emitted by a runtime probe; it does not infer live RAM
from a config file or package name.  The receipt must prove that 8 MB was
selected before BIOS/game initialization and that the same identity survived
DMA and save-state handling.
"""
import argparse
import json
from pathlib import Path


RAM_BYTES = 8 * 1024 * 1024
TWO_MB_BYTES = 2 * 1024 * 1024
HIGH_RAM_START = 0x80200000


def validate(receipt):
    errors = []
    if not isinstance(receipt, dict):
        return ["receipt must be a JSON object"]
    if receipt.get("pass") is not True:
        errors.append("receipt pass must be true")
    if receipt.get("live_ram_bytes") != RAM_BYTES:
        errors.append("live_ram_bytes must be exactly 8388608")
    if receipt.get("fallback_detected") is not False:
        errors.append("fallback_detected must be false")
    if receipt.get("fallback_ram_bytes") not in (None, RAM_BYTES):
        errors.append("fallback_ram_bytes must be absent or 8388608")

    order = receipt.get("initialization_order")
    required_order = ("ram_activation", "memory_init", "bios_init", "game_init")
    if not isinstance(order, list):
        errors.append("initialization_order must be a list")
    else:
        positions = {name: order.index(name) for name in required_order if name in order}
        missing = [name for name in required_order if name not in positions]
        if missing:
            errors.append("initialization_order missing: " + ", ".join(missing))
        elif not all(positions[a] < positions[b] for a, b in zip(required_order, required_order[1:])):
            errors.append("8 MB activation must precede memory, BIOS, and game initialization")

    probe = receipt.get("high_address_probe")
    if not isinstance(probe, dict):
        errors.append("high_address_probe is required")
    else:
        address = probe.get("address")
        if not isinstance(address, int) or address < HIGH_RAM_START:
            errors.append("high_address_probe.address must be at or above 0x80200000")
        if probe.get("write_pass") is not True or probe.get("read_pass") is not True:
            errors.append("high_address_probe must pass both write and read")
        if probe.get("value_verified") is not True:
            errors.append("high_address_probe.value_verified must be true")

    dma = receipt.get("dma_probe")
    if not isinstance(dma, dict):
        errors.append("dma_probe is required")
    else:
        if dma.get("pass") is not True or dma.get("high_address_used") is not True:
            errors.append("dma_probe must pass using a high-RAM address")

    state = receipt.get("savestate_metadata")
    if not isinstance(state, dict):
        errors.append("savestate_metadata is required")
    else:
        if state.get("live_ram_bytes") != RAM_BYTES or state.get("ram_section_bytes") != RAM_BYTES:
            errors.append("save-state metadata must record 8388608 live and section bytes")
        if state.get("ram_size_match") is not True:
            errors.append("save-state RAM size match must be true")

    pairing = receipt.get("pairing")
    if not isinstance(pairing, dict) or pairing.get("ram_bytes") != RAM_BYTES or pairing.get("overclock_percent") != 900 or pairing.get("pass") is not True:
        errors.append("pairing must pass for 8388608 bytes with 900% overclock")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path)
    args = parser.parse_args()
    with args.receipt.open(encoding="utf-8") as stream:
        receipt = json.load(stream)
    errors = validate(receipt)
    if errors:
        print("FAIL 8 MB runtime receipt")
        for error in errors:
            print(f"- {error}")
        return 1
    print("PASS 8 MB runtime receipt: pre-BIOS live map, high-RAM read/write, DMA, save-state, and 900% pairing verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
