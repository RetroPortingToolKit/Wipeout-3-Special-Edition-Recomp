#!/usr/bin/env python3
"""Create a verified, ignored PAL-patched working disc from user-owned media."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

import verify_media


FILE_RE = re.compile(r'^(\s*FILE\s+)(?:"([^"]+)"|(\S+))(\s+\S+\s*)$', re.I)


def copy_file(source: Path, destination: Path) -> None:
    if destination.exists():
        raise verify_media.VerificationError(f"refusing to overwrite staged file: {destination}")
    shutil.copy2(source, destination)


def rewrite_cue(source_cue: Path, destination_cue: Path, staged_names: dict[Path, str]) -> None:
    output: list[str] = []
    for line_number, line in enumerate(source_cue.read_text(encoding="utf-8-sig").splitlines(), 1):
        match = FILE_RE.match(line)
        if not match:
            output.append(line)
            continue
        relative = match.group(2) or match.group(3)
        assert relative is not None
        source_path = (source_cue.parent / Path(relative.replace("\\", os.sep))).resolve()
        if source_path not in staged_names:
            raise verify_media.VerificationError(
                f"unmapped FILE entry at {source_cue}:{line_number}: {relative}"
            )
        output.append(f'{match.group(1)}"{staged_names[source_path]}"{match.group(4)}')
    destination_cue.write_text("\n".join(output) + "\n", encoding="utf-8", newline="\n")


def stage(args: argparse.Namespace) -> dict[str, object]:
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    source_tracks = verify_media.parse_cue(args.cue)
    expected_count = manifest["game"]["cue_tracks"]
    verify_media.require_equal("CUE track count", len(source_tracks), expected_count)

    if args.output.exists():
        raise verify_media.VerificationError(f"refusing to overwrite output directory: {args.output}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f"{args.output.name}.tmp-", dir=args.output.parent))
    try:
        track_kind, _ = verify_media.identify_track_01(
            source_tracks[0]["path"], manifest, args.b3sum
        )
        staged_names: dict[Path, str] = {}
        used_names: set[str] = set()
        for item in source_tracks:
            source = item["path"].resolve()
            name = source.name
            if name.lower() in used_names:
                raise verify_media.VerificationError(f"duplicate staged basename: {name}")
            used_names.add(name.lower())
            staged_names[source] = name

        staged_track_01 = temporary / staged_names[source_tracks[0]["path"].resolve()]
        if track_kind == "redump":
            if not args.patch or not args.xdelta3:
                raise verify_media.VerificationError(
                    "unmodified Redump media requires --patch and --xdelta3"
                )
            verify_media.verify_file(args.patch, manifest["hueponik_patch"], args.b3sum)
            verify_media.apply_patch(
                source_tracks[0]["path"], args.patch, staged_track_01, args.xdelta3
            )
        elif track_kind == "hueponik_pal":
            copy_file(source_tracks[0]["path"], staged_track_01)
        else:  # pragma: no cover - identify_track_01 rejects unknown identities
            raise verify_media.VerificationError(f"unsupported Track 01 identity: {track_kind}")

        for item in source_tracks[1:]:
            copy_file(item["path"], temporary / staged_names[item["path"].resolve()])

        staged_cue = temporary / args.cue.name
        rewrite_cue(args.cue, staged_cue, staged_names)

        verify_args = argparse.Namespace(
            cue=staged_cue,
            manifest=args.manifest,
            patch=args.patch,
            xdelta3=None,
            output=None,
            bios=args.bios,
            b3sum=args.b3sum,
            json_out=None,
        )
        report = verify_media.verify(verify_args)
        verify_media.require_equal(
            "staged Track 01 identity", report["track_01"]["identity"], "hueponik_pal"
        )
        temporary.replace(args.output)
        report["prepared_cue"] = args.output.name + "/" + staged_cue.name
        report["source_track_01_identity"] = track_kind
        return report
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cue", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=verify_media.DEFAULT_MANIFEST)
    parser.add_argument("--patch", type=Path)
    parser.add_argument("--xdelta3")
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--b3sum")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        report = stage(args)
        sys.stdout.write(json.dumps(report, indent=2, sort_keys=True) + "\n")
        return 0
    except (OSError, ValueError, verify_media.VerificationError) as error:
        sys.stderr.write(f"media preparation failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
