#!/usr/bin/env python3
"""Inject a PS1 pad route into a neutral RetroArch v2 replay.

This is behavior-oracle tooling only.  It edits input-event values in place and
does not copy emulator code into the clean-AOT product.  The binary layout was
validated against RetroArch v1.22.2 (69a4f0ea1e8aaf442ae4858f2e7f2b31a1776576).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any

REPLAY_MAGIC = 0x42535632
REPLAY_VERSION = 2
HEADER = struct.Struct("<10I")
INPUT_EVENT = struct.Struct("<BBBBHh")
KEY_EVENT_SIZE = 12
REGULAR_FRAME = ord("f")
CHECKPOINT_FRAME = ord("c")
CHECKPOINT2_FRAME = ord("C")

RETRO_DEVICE_JOYPAD = 1

# Raw PS1 digital-pad bits are active-low.  RetroPad button IDs are active-high.
PS1_BIT_TO_RETROPAD_ID = {
    0: 2,  # Select
    1: 14,  # L3
    2: 15,  # R3
    3: 3,  # Start
    4: 4,  # Up
    5: 7,  # Right
    6: 5,  # Down
    7: 6,  # Left
    8: 12,  # L2
    9: 13,  # R2
    10: 10,  # L1
    11: 11,  # R1
    12: 9,  # Triangle
    13: 8,  # Circle
    14: 0,  # Cross
    15: 1,  # Square
}


class ReplayFormatError(ValueError):
    """The replay or route violates the bounded format accepted by this tool."""


@dataclass(frozen=True)
class Frame:
    index: int
    value_offsets: dict[int, int]


def _require(data: bytes | bytearray, pos: int, size: int, label: str) -> None:
    if pos < 0 or size < 0 or pos + size > len(data):
        raise ReplayFormatError(f"truncated {label} at byte {pos}")


def parse_frames(data: bytes | bytearray) -> tuple[tuple[int, ...], list[Frame]]:
    _require(data, 0, HEADER.size, "header")
    header = HEADER.unpack_from(data)
    if header[0] != REPLAY_MAGIC:
        raise ReplayFormatError(f"unexpected replay magic 0x{header[0]:08X}")
    if header[1] != REPLAY_VERSION:
        raise ReplayFormatError(
            f"unsupported replay version {header[1]}; expected {REPLAY_VERSION}"
        )

    pos = HEADER.size + header[3]
    _require(data, HEADER.size, header[3], "initial checkpoint")
    frames: list[Frame] = []

    while pos < len(data):
        _require(data, pos, 5, "frame prefix")
        pos += (
            4  # Back-reference; values stay valid because record sizes do not change.
        )
        key_count = data[pos]
        pos += 1
        key_bytes = key_count * KEY_EVENT_SIZE
        _require(data, pos, key_bytes + 2, "keyboard/input counts")
        pos += key_bytes
        event_count = struct.unpack_from("<H", data, pos)[0]
        pos += 2

        value_offsets: dict[int, int] = {}
        for _ in range(event_count):
            _require(data, pos, INPUT_EVENT.size, "input event")
            port, device, index, _padding, event_id, _value = INPUT_EVENT.unpack_from(
                data, pos
            )
            if port == 0 and device == RETRO_DEVICE_JOYPAD and index == 0:
                if event_id in value_offsets:
                    raise ReplayFormatError(
                        f"duplicate port-0 joypad ID {event_id} in frame {len(frames)}"
                    )
                value_offsets[event_id] = pos + 6
            pos += INPUT_EVENT.size

        _require(data, pos, 1, "frame token")
        token = data[pos]
        pos += 1
        if token == REGULAR_FRAME:
            pass
        elif token == CHECKPOINT_FRAME:
            _require(data, pos, 8, "legacy checkpoint length")
            payload_size = struct.unpack_from("<Q", data, pos)[0]
            pos += 8
            _require(data, pos, payload_size, "legacy checkpoint payload")
            pos += payload_size
        elif token == CHECKPOINT2_FRAME:
            _require(data, pos, 14, "checkpoint2 header")
            # compression, encoding, raw size, encoded size, stored size
            stored_size = struct.unpack_from("<I", data, pos + 10)[0]
            pos += 14
            _require(data, pos, stored_size, "checkpoint2 payload")
            pos += stored_size
        else:
            raise ReplayFormatError(
                f"unknown frame token 0x{token:02X} in frame {len(frames)}"
            )

        frames.append(Frame(index=len(frames), value_offsets=value_offsets))

    if pos != len(data):
        raise ReplayFormatError("replay parser did not terminate at EOF")
    if header[6] != len(frames):
        raise ReplayFormatError(
            f"header declares {header[6]} frames but parsed {len(frames)}"
        )
    return header, frames


def _parse_pad_word(value: Any, label: str) -> int:
    if isinstance(value, bool):
        raise ReplayFormatError(f"{label} must be a 16-bit integer")
    if isinstance(value, int):
        parsed = value
    elif isinstance(value, str):
        try:
            parsed = int(value, 0)
        except ValueError as exc:
            raise ReplayFormatError(f"{label} is not an integer: {value!r}") from exc
    else:
        raise ReplayFormatError(f"{label} must be an integer or integer string")
    if not 0 <= parsed <= 0xFFFF:
        raise ReplayFormatError(f"{label} is outside 0x0000..0xFFFF")
    return parsed


def load_route(path: Path) -> list[tuple[int, int]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ReplayFormatError(f"cannot read route {path}: {exc}") from exc
    if (
        not isinstance(document, dict)
        or document.get("format") != "psxrecomp-input-route-v1"
    ):
        raise ReplayFormatError("route format must be psxrecomp-input-route-v1")
    raw_steps = document.get("steps")
    if not isinstance(raw_steps, list) or not raw_steps:
        raise ReplayFormatError("route steps must be a non-empty array")

    steps: list[tuple[int, int]] = []
    for index, raw_step in enumerate(raw_steps):
        if not isinstance(raw_step, dict):
            raise ReplayFormatError(f"steps[{index}] must be an object")
        pad_word = _parse_pad_word(raw_step.get("pad_word"), f"steps[{index}].pad_word")
        frames = raw_step.get("frames")
        if isinstance(frames, bool) or not isinstance(frames, int) or frames <= 0:
            raise ReplayFormatError(f"steps[{index}].frames must be a positive integer")
        steps.append((pad_word, frames))
    return steps


def inject_route(
    data: bytearray, frames: list[Frame], steps: list[tuple[int, int]], start_frame: int
) -> int:
    if start_frame < 0:
        raise ReplayFormatError("start frame must be non-negative")
    route_length = sum(frame_count for _pad_word, frame_count in steps)
    end_frame = start_frame + route_length
    if end_frame > len(frames):
        raise ReplayFormatError(
            f"route ends at frame {end_frame}, beyond replay frame count {len(frames)}"
        )

    patched_values = 0
    frame_index = start_frame
    required_ids = set(PS1_BIT_TO_RETROPAD_ID.values())
    for pad_word, frame_count in steps:
        active_ids = {
            retro_id
            for ps1_bit, retro_id in PS1_BIT_TO_RETROPAD_ID.items()
            if (pad_word & (1 << ps1_bit)) == 0
        }
        for _ in range(frame_count):
            frame = frames[frame_index]
            missing = required_ids.difference(frame.value_offsets)
            if missing:
                raise ReplayFormatError(
                    f"frame {frame_index} lacks RetroPad IDs {sorted(missing)}"
                )
            for retro_id in required_ids:
                struct.pack_into(
                    "<h",
                    data,
                    frame.value_offsets[retro_id],
                    1 if retro_id in active_ids else 0,
                )
                patched_values += 1
            frame_index += 1
    return patched_values


def sha256_bytes(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input", type=Path, required=True, help="neutral RetroArch v2 replay"
    )
    parser.add_argument(
        "--route", type=Path, required=True, help="psxrecomp input-route v1 JSON"
    )
    parser.add_argument(
        "--start-frame", type=int, required=True, help="zero-based replay frame"
    )
    parser.add_argument(
        "--output", type=Path, required=True, help="patched replay path"
    )
    parser.add_argument("--receipt", type=Path, help="optional canonical JSON receipt")
    args = parser.parse_args()

    if args.input.resolve() == args.output.resolve():
        parser.error("--output must differ from --input")

    try:
        source = args.input.read_bytes()
        data = bytearray(source)
        header, frames = parse_frames(data)
        steps = load_route(args.route)
        patched_values = inject_route(data, frames, steps, args.start_frame)
    except (OSError, ReplayFormatError) as exc:
        parser.error(str(exc))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, args.output)

    route_length = sum(frame_count for _pad_word, frame_count in steps)
    receipt = {
        "format": "wipeout3se.goosestation-route-replay.v1",
        "retroarch": {
            "commit": "69a4f0ea1e8aaf442ae4858f2e7f2b31a1776576",
            "replay_version": header[1],
        },
        "input_replay_sha256": sha256_bytes(source),
        "output_replay_sha256": sha256_bytes(data),
        "route_sha256": hashlib.sha256(args.route.read_bytes()).hexdigest(),
        "replay_frames": len(frames),
        "route_start_frame": args.start_frame,
        "route_end_frame_exclusive": args.start_frame + route_length,
        "route_frames": route_length,
        "patched_input_values": patched_values,
    }
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(
            json.dumps(receipt, sort_keys=True, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
    print(json.dumps(receipt, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
