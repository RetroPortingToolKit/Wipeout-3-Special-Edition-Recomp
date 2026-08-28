#!/usr/bin/env python3
"""Verify the complete Hueponik PAL menu contract from authenticated artifacts.

This is an evidence verifier, not an installer.  It reads the pinned patched
SCES-02845 executable and decodes both the resident 2 MiB menu and the 8 MiB
runtime-patch tables embedded in that executable.  No Hueponik bytes are
proposed for use by the direct implementation.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


STOCK_SHA256 = "0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d"
PATCHED_SHA256 = "7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac"
LOAD_ADDRESS = 0x80010000
EXE_HEADER = 0x800

# The 8 MiB data is embedded at patched-EXE file offset 0xC0800.  Hueponik's
# loader relocates the menu-data subrange with a 0x48-byte displacement.
EMBEDDED_FILE_OFFSET = 0xC0800
HIGH_DATA_BASE = 0x8077FA48
HIGH_PATCH_TABLE_VA = 0x800D2648
HIGH_PATCH_PAIR_COUNT = 424

LABELS_8M = (
    "FRAME RATE",
    "SHIP LOD",
    "SHIP DIST",
    "TRACK 4X4",
    "DRAW DIST",
    "BIRDS",
)
OPTIONS_8M = (
    ("25HZ", "50HZ", "100HZ"),
    (
        "0", "600", "STOCK", "1800", "2400", "3000", "3600",
        "4200", "4800", "5400", "6000", "6600", "7200", "7800",
        "8400", "9000", "9600", "10200", "10800", "11400",
        "12000", "MAX",
    ),
    ("STOCK", "24K", "40K", "52K", "65K"),
    ("OFF", "ON"),
    ("LOW", "STOCK", "NEAR", "MID", "FAR"),
    ("LOW", "STOCK", "HIGH"),
)
DEFAULTS_8M = (1, 21, 4, 1, 4, 2)

LABELS_2M = ("FRAME RATE", "SHIP LOD", "SHIP DIST", "BIRDS")
OPTIONS_2M = (
    ("25HZ", "50HZ", "100HZ"),
    ("0", "STOCK", "2400", "3600", "4800", "6000", "7200", "9600", "12000", "MAX"),
    ("STOCK", "26K", "29K", "32K"),
    ("LOW", "STOCK", "HIGH"),
)
DEFAULTS_2M = (1, 1, 0, 1)

EXPECTED_DESCRIPTORS = {
    "2M SHIP LOD": (
        0x8017BCD8, (0x80130A20, 0x80130A24), 4,
        (
            (0xFFFF, 0xFFFF), (0x0015, 0xF8FF), (0x0057, 0xE3FF),
            (0x00C5, 0xC0FF), (0x015F, 0x8FFF), (0x0225, 0x50FF),
            (0x0317, 0x03FF), (0x057E, 0x3FFF), (0x0895, 0x43FF),
            (0x7FFF, 0xFFFF),
        ),
    ),
    "2M SHIP DIST": (
        0x8017BC6C,
        (0x80130A0C, 0x80130A10, 0x801308E4, 0x801308FC,
         0x8013085C, 0x8013086C, 0x80152974, 0x80152978),
        16,
        (
            (0x0080, 0x538F, 0x0BB9, 0x0BB9, 0x0089, 0x5440, 0x0080, 0x538F),
            (0x00A1, 0x2BC4, 0x0CB2, 0x0CB2, 0x7FFF, 0xFFFF, 0x00A1, 0x2BC4),
            (0x00C8, 0x8291, 0x0E29, 0x0E29, 0x7FFF, 0xFFFF, 0x00C8, 0x8291),
            (0x00F4, 0x2400, 0x0FA0, 0x0FA0, 0x7FFF, 0xFFFF, 0x00F4, 0x2400),
        ),
    ),
    "2M BIRDS": (
        0x8017BC58, (0x800FA224, 0x800FA226), 4,
        ((0x0000, 0x3C02), (0x3000, 0x3402), (0x7FFF, 0x3C02)),
    ),
    "8M SHIP LOD": (
        0x80781D1C, (0x80130A20, 0x80130A24), 4,
        (
            (0xFFFF, 0xFFFF), (0x0005, 0x7E3F), (0x0015, 0xF8FF),
            (0x0031, 0x703F), (0x0057, 0xE3FF), (0x0089, 0x543F),
            (0x00C5, 0xC0FF), (0x010D, 0x2A3F), (0x015F, 0x8FFF),
            (0x01BC, 0xF23F), (0x0225, 0x50FF), (0x0298, 0xAC3F),
            (0x0317, 0x03FF), (0x03A0, 0x583F), (0x0434, 0xA8FF),
            (0x04D3, 0xF63F), (0x057E, 0x3FFF), (0x0633, 0x863F),
            (0x06F3, 0xC8FF), (0x07BF, 0x083F), (0x0895, 0x43FF),
            (0x7FFF, 0xFFFF),
        ),
    ),
    "8M SHIP DIST": (
        0x80781D80,
        (0x80130A0C, 0x80130A10, 0x801308E4, 0x801308FC,
         0x8013085C, 0x8013086C, 0x80152974, 0x80152978),
        16,
        (
            (0x0080, 0x538F, 0x0BB9, 0x0BB9, 0x0089, 0x5440, 0x0080, 0x538F),
            (0x0089, 0x5440, 0x0BB8, 0x0BB8, 0x7FFF, 0xFFFF, 0x0089, 0x5440),
            (0x017D, 0x7840, 0x1388, 0x1388, 0x7FFF, 0xFFFF, 0x017D, 0x7840),
            (0x0284, 0xAF10, 0x1964, 0x1964, 0x7FFF, 0xFFFF, 0x0284, 0xAF10),
            (0x03FF, 0xC001, 0x1FFF, 0x1FFF, 0x7FFF, 0xFFFF, 0x03FF, 0xC001),
        ),
    ),
    "8M TRACK 4X4": (
        0x80781DBC,
        (0x805FF7C4, 0x805FF7C6, 0x805FF7D4, 0x805FF7D6),
        8,
        ((0, 0, 1, 0), (1, 0, 1, 0)),
    ),
    "8M DRAW DIST": (
        0x80781E3C,
        (0x805FF7EC, 0x805FF7EE, 0x805FF7CC, 0x805FF7CE,
         0x805FF7E8, 0x805FF7EA, 0x805FF7D0, 0x805FF7D2,
         0x805FF7D4, 0x805FF7D6),
        20,
        (
            (0x3E80, 0, 0x36B0, 0, 0x2EE0, 0, 2, 0, 1, 0),
            (0x9C40, 0, 0x84D0, 0, 0x7D00, 0, 1, 0, 1, 0),
            (0x9C40, 0, 0x84D0, 0, 0x7D00, 0, 0, 0, 1, 0),
            (0xDAC0, 0, 0xBB80, 0, 0xABE0, 0, 0, 0, 1, 0),
            (0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0, 0, 1, 0),
        ),
    ),
    "8M BIRDS": (
        0x80781E7C, (0x800FA224, 0x800FA226), 4,
        ((0x0000, 0x3C02), (0x3000, 0x3402), (0x7FFF, 0x3C02)),
    ),
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class Image:
    def __init__(self, stock: bytes, patched: bytes):
        self.stock = stock
        self.patched = patched

    @staticmethod
    def low_offset(address: int) -> int:
        return EXE_HEADER + address - LOAD_ADDRESS

    def low(self, address: int, size: int) -> bytes:
        offset = self.low_offset(address)
        return self.patched[offset:offset + size]

    def high(self, address: int, size: int) -> bytes:
        offset = EMBEDDED_FILE_OFFSET + address - HIGH_DATA_BASE
        return self.patched[offset:offset + size]

    def memory(self, address: int, size: int) -> bytes:
        if HIGH_DATA_BASE <= address < 0x80790000:
            return self.high(address, size)
        return self.low(address, size)

    def u16(self, address: int) -> int:
        return struct.unpack("<H", self.memory(address, 2))[0]

    def u32(self, address: int) -> int:
        return struct.unpack("<I", self.memory(address, 4))[0]

    def cstr(self, address: int) -> str:
        data = self.memory(address, 128)
        return data.split(b"\0", 1)[0].decode("ascii")


def decode_menu(image: Image, count: int, labels_at: int, values_at: int,
                counts_at: int) -> tuple[tuple[str, ...], tuple[tuple[str, ...], ...]]:
    labels = tuple(image.cstr(image.u32(labels_at + 4 * i)) for i in range(count))
    options = []
    for i in range(count):
        option_count = image.u32(counts_at + 4 * i)
        option_base = image.u32(values_at + 4 * i)
        options.append(tuple(image.cstr(image.u32(option_base + 4 * j))
                             for j in range(option_count)))
    return labels, tuple(options)


def decode_descriptor(image: Image, address: int, option_count: int):
    write_count = image.u32(address)
    stride = image.u32(address + 4)
    values = image.u32(address + 8)
    destinations = tuple(image.u32(address + 12 + 4 * i)
                         for i in range(write_count))
    rows = tuple(
        tuple(image.u16(values + option * stride + 2 * i)
              for i in range(write_count))
        for option in range(option_count)
    )
    return destinations, stride, rows


def read_high_patch_pairs(image: Image) -> dict[int, int]:
    offset = image.low_offset(HIGH_PATCH_TABLE_VA)
    pairs: dict[int, int] = {}
    for i in range(HIGH_PATCH_PAIR_COUNT):
        address, value = struct.unpack_from("<II", image.patched, offset + 8 * i)
        if address in pairs:
            raise AssertionError(f"duplicate 8 MiB patch target 0x{address:08X}")
        pairs[address] = value
    assert struct.unpack_from("<II", image.patched,
                              offset + 8 * HIGH_PATCH_PAIR_COUNT) == (0, 0)
    return pairs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    stock = (root / "disc/SCES_028.45").read_bytes()
    patched = (root / "aot/SCES_028.45").read_bytes()
    assert digest(stock) == STOCK_SHA256, "stock executable digest mismatch"
    assert digest(patched) == PATCHED_SHA256, "patched executable digest mismatch"
    image = Image(stock, patched)

    labels, options = decode_menu(image, 4, 0x8017BC48, 0x8017BC38, 0x8017BC28)
    assert labels == LABELS_2M
    assert options == OPTIONS_2M
    defaults_2m = tuple(image.u32(0x8017B8EC + 4 * i) for i in range(4))
    assert defaults_2m == DEFAULTS_2M

    labels, options = decode_menu(image, 6, 0x80781E90, 0x80781EA8, 0x80781EC0)
    assert labels == LABELS_8M
    assert options == OPTIONS_8M

    pairs = read_high_patch_pairs(image)
    defaults_8m = tuple(pairs.get(0x8017B8EC + 4 * i,
                                  image.u32(0x8017B8EC + 4 * i))
                        for i in range(6))
    assert defaults_8m == DEFAULTS_8M

    descriptor_counts = {
        "2M SHIP LOD": 10, "2M SHIP DIST": 4, "2M BIRDS": 3,
        "8M SHIP LOD": 22, "8M SHIP DIST": 5, "8M TRACK 4X4": 2,
        "8M DRAW DIST": 5, "8M BIRDS": 3,
    }
    for name, (address, destinations, stride, rows) in EXPECTED_DESCRIPTORS.items():
        assert decode_descriptor(image, address, descriptor_counts[name]) == (
            destinations, stride, rows), name

    # Exact selector publication and 8 MiB menu/input detours.
    expected_patch_words = {
        0x80123EF8: 0x0C05EE48,  # UpdatePerformanceTuningMenu, 8 MiB entry
        0x80134740: 0x0C05EEE9,  # controller aggregation wrapper
        0x8017B9A0: 0x340F0006,  # six-row cursor wrap
        0x8017B9B8: 0x2DE10006,
        0x8017BA30: 0x2E210006,  # six publisher descriptors
        0x8017BABC: 0x2E210006,  # six rendered rows
        0x8017BB70: 0xAD2CF02C,  # gSimVblankSkip = (selector < 1) + 1
        0x8017BB80: 0xAD4C5064,  # PAL-x2 = !(selector < 2)
        0x800FA224: 0x3C027FFF,  # default BIRDS HIGH
        0x80130A20: 0x3C047FFF,  # default SHIP LOD MAX, first word
        0x80130A24: 0x3484FFFF,
        0x80130A0C: 0x3C0203FF,  # default SHIP DIST 65K, first consumer
        0x80130A10: 0x3442C001,
        0x80152974: 0x3C0203FF,  # default SHIP DIST 65K, second consumer
        0x80152978: 0x3442C001,
    }
    for address, value in expected_patch_words.items():
        assert pairs.get(address) == value, f"8 MiB patch 0x{address:08X}"

    # Track 4x4 is wholly replacement-engine state.  None of its destinations
    # is an authenticated stock executable address.
    track_destinations = EXPECTED_DESCRIPTORS["8M TRACK 4X4"][1]
    assert all(address >= 0x80500000 for address in track_destinations)
    assert set(track_destinations) == {0x805FF7C4, 0x805FF7C6, 0x805FF7D4, 0x805FF7D6}

    print("PASS: authenticated Hueponik menu is exactly 6 settings in 8 MiB ")
    print("      and 4 settings in 2 MiB; all labels, choices, defaults, ")
    print("      publisher descriptors, selector stores, and consumer words match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
