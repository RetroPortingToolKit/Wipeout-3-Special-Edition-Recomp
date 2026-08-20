#!/usr/bin/env python3
"""Verify user-owned WipEout 3 SE media and the pinned hueponik PAL patch.

The tool never modifies source media. Patch application requires a new,
explicit output path and refuses to overwrite it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zlib
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / "assets" / "manifests" / "wipeout3se-media.json"
FILE_RE = re.compile(r'^\s*FILE\s+(?:"([^"]+)"|(\S+))\s+(\S+)\s*$', re.I)
TRACK_RE = re.compile(r"^\s*TRACK\s+(\d+)\s+(\S+)\s*$", re.I)
BUFFER_SIZE = 8 * 1024 * 1024


class VerificationError(RuntimeError):
    pass


def digest_file(path: Path, algorithms: Iterable[str], include_crc32: bool = False) -> dict[str, str]:
    hashers = {name: hashlib.new(name) for name in algorithms}
    crc = 0
    with path.open("rb") as stream:
        while chunk := stream.read(BUFFER_SIZE):
            for hasher in hashers.values():
                hasher.update(chunk)
            if include_crc32:
                crc = zlib.crc32(chunk, crc)
    result = {name: hasher.hexdigest() for name, hasher in hashers.items()}
    if include_crc32:
        result["crc32"] = f"{crc & 0xFFFFFFFF:08x}"
    return result


def blake3_file(path: Path, executable: str | None) -> str | None:
    command = executable or shutil.which("b3sum")
    if not command:
        return None
    completed = subprocess.run(
        [command, "--no-names", os.fspath(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    value = completed.stdout.strip().split()[0].lower()
    if not re.fullmatch(r"[0-9a-f]{64}", value):
        raise VerificationError(f"unexpected b3sum output for {path.name!r}: {completed.stdout!r}")
    return value


def parse_cue(cue_path: Path) -> list[dict[str, Any]]:
    current_file: Path | None = None
    tracks: list[dict[str, Any]] = []
    for line_number, line in enumerate(cue_path.read_text(encoding="utf-8-sig").splitlines(), 1):
        file_match = FILE_RE.match(line)
        if file_match:
            relative = file_match.group(1) or file_match.group(2)
            assert relative is not None
            current_file = cue_path.parent / Path(relative.replace("\\", os.sep))
            continue
        track_match = TRACK_RE.match(line)
        if track_match:
            if current_file is None:
                raise VerificationError(f"TRACK before FILE at {cue_path}:{line_number}")
            tracks.append(
                {
                    "number": int(track_match.group(1)),
                    "mode": track_match.group(2).upper(),
                    "path": current_file,
                }
            )
    return tracks


def require_equal(label: str, actual: Any, expected: Any) -> None:
    if actual != expected:
        raise VerificationError(f"{label}: expected {expected!r}, got {actual!r}")


def verify_file(path: Path, expected: dict[str, Any], b3sum: str | None = None) -> dict[str, Any]:
    if not path.is_file():
        raise VerificationError(f"missing file: {path}")
    require_equal(f"{path.name} size", path.stat().st_size, expected["size"])
    algorithms = [name for name in ("md5", "sha1", "sha256") if name in expected]
    actual = digest_file(path, algorithms, include_crc32="crc32" in expected)
    for algorithm, value in actual.items():
        require_equal(f"{path.name} {algorithm}", value.lower(), expected[algorithm].lower())
    if "blake3" in expected:
        value = blake3_file(path, b3sum)
        if value is not None:
            require_equal(f"{path.name} blake3", value, expected["blake3"].lower())
            actual["blake3"] = value
        else:
            actual["blake3"] = "not-checked"
    return {"name": path.name, "size": path.stat().st_size, "hashes": actual}


def identify_track_01(path: Path, manifest: dict[str, Any], b3sum: str | None) -> tuple[str, dict[str, Any]]:
    identities = manifest["track_01"]
    algorithms = ("md5", "sha1", "sha256")
    actual = digest_file(path, algorithms, include_crc32=True)
    actual_b3 = blake3_file(path, b3sum)
    for name, expected in identities.items():
        if path.stat().st_size != expected["size"]:
            continue
        comparable = {key: value for key, value in expected.items() if key in actual}
        if all(actual[key].lower() == value.lower() for key, value in comparable.items()):
            if actual_b3 is not None and "blake3" in expected:
                require_equal(f"{path.name} blake3", actual_b3, expected["blake3"].lower())
            report_hashes = {key: actual[key] for key in comparable}
            report_hashes["blake3"] = actual_b3 or "not-checked"
            return name, {"name": path.name, "size": path.stat().st_size, "hashes": report_hashes}
    raise VerificationError(
        f"{path.name} matches neither the Redump source nor pinned hueponik PAL target; "
        f"md5={actual['md5']} sha256={actual['sha256']}"
    )


def apply_patch(source: Path, patch: Path, output: Path, xdelta3: str) -> None:
    if output.exists():
        raise VerificationError(f"refusing to overwrite patch output: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    completed = subprocess.run(
        [xdelta3, "-d", "-s", os.fspath(source), os.fspath(patch), os.fspath(output)],
        text=True,
        capture_output=True,
    )
    if completed.returncode:
        if output.exists():
            output.unlink()
        raise VerificationError(
            f"xdelta3 failed with {completed.returncode}: "
            f"{completed.stderr.strip() or completed.stdout.strip()}"
        )


def verify(args: argparse.Namespace) -> dict[str, Any]:
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    tracks = parse_cue(args.cue)
    expected_count = manifest["game"]["cue_tracks"]
    require_equal("CUE track count", len(tracks), expected_count)
    require_equal("CUE track numbering", [item["number"] for item in tracks], list(range(1, expected_count + 1)))
    require_equal("track 1 mode", tracks[0]["mode"], "MODE2/2352")
    for item in tracks[1:]:
        require_equal(f"track {item['number']} mode", item["mode"], "AUDIO")

    track_kind, track_report = identify_track_01(tracks[0]["path"], manifest, args.b3sum)
    audio_report = []
    expected_audio = {item["track"]: item for item in manifest["audio_tracks"]}
    for item in tracks[1:]:
        audio_report.append(verify_file(item["path"], expected_audio[item["number"]]))

    patch_report = None
    output_report = None
    if args.patch:
        patch_report = verify_file(args.patch, manifest["hueponik_patch"], args.b3sum)
    if args.output:
        if not args.patch or not args.xdelta3:
            raise VerificationError("--output requires --patch and --xdelta3")
        if track_kind != "redump":
            raise VerificationError("patch application requires an unmodified Redump Track 01 source")
        apply_patch(tracks[0]["path"], args.patch, args.output, args.xdelta3)
        output_report = verify_file(args.output, manifest["track_01"]["hueponik_pal"], args.b3sum)

    bios_report = None
    if args.bios:
        bios_report = verify_file(args.bios, manifest["bios"]["scph1001"])

    return {
        "format_version": 1,
        "status": "ok",
        "game_id": manifest["game"]["id"],
        "cue": {"name": args.cue.name, "tracks": len(tracks)},
        "track_01": {"identity": track_kind, **track_report},
        "audio_tracks": audio_report,
        "patch": patch_report,
        "patched_output": output_report,
        "bios": bios_report,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cue", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--patch", type=Path)
    parser.add_argument("--xdelta3")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--b3sum", help="path to b3sum; auto-detected when omitted")
    parser.add_argument("--json-out", type=Path, help="optional new evidence file; existing files are refused")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        report = verify(args)
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            if args.json_out.exists():
                raise VerificationError(f"refusing to overwrite evidence output: {args.json_out}")
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(payload, encoding="utf-8", newline="\n")
        sys.stdout.write(payload)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError, VerificationError) as error:
        sys.stderr.write(f"media verification failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
