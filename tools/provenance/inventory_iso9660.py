#!/usr/bin/env python3
"""Emit a deterministic, hash-only ISO9660 file inventory."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from contextlib import suppress
from pathlib import Path, PurePosixPath
from typing import Any

try:
    import pycdlib
except ImportError:  # pragma: no cover - exercised by deployment preflight
    pycdlib = None


BUFFER_SIZE = 8 * 1024 * 1024


class InventoryError(RuntimeError):
    pass


class DigestSink:
    def __init__(self, prefix_limit: int = 8) -> None:
        self.digest = hashlib.sha256()
        self.prefix = bytearray()
        self.prefix_limit = prefix_limit
        self.bytes = 0

    def write(self, data: bytes) -> int:
        if len(self.prefix) < self.prefix_limit:
            remaining = self.prefix_limit - len(self.prefix)
            self.prefix.extend(data[:remaining])
        self.digest.update(data)
        self.bytes += len(data)
        return len(data)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(BUFFER_SIZE):
            digest.update(chunk)
    return digest.hexdigest()


def extraction_destination(root: Path, logical: object) -> Path:
    """Map one logical ISO path below ``root`` without host-path reinterpretation."""

    value = str(logical)
    if "\\" in value or ":" in value:
        raise InventoryError(f"unsafe logical ISO path: {value}")
    relative = PurePosixPath(value.lstrip("/"))
    if (
        not relative.parts
        or relative.is_absolute()
        or any(part in ("", ".", "..") for part in relative.parts)
    ):
        raise InventoryError(f"unsafe logical ISO path: {value}")
    return root.joinpath(*relative.parts)


def logical_path(iso_path: str) -> str:
    parts = []
    for part in iso_path.split("/"):
        if not part:
            continue
        parts.append(part.rsplit(";", 1)[0])
    return "/" + "/".join(parts)


def inventory(iso_path: Path, include_file_hashes: bool = True) -> dict[str, Any]:
    if pycdlib is None:
        raise InventoryError("pycdlib 1.14.0 is required for ISO9660 inventory")
    if not iso_path.is_file():
        raise InventoryError(f"missing ISO image: {iso_path}")

    image = pycdlib.PyCdlib()
    try:
        image.open(str(iso_path))
        image_bytes = iso_path.stat().st_size
        entries: list[dict[str, Any]] = []
        for root, _directories, files in image.walk(iso_path="/"):
            for name in files:
                exact_path = root.rstrip("/") + "/" + name
                record = image.get_record(iso_path=exact_path)
                entry: dict[str, Any] = {
                    "iso_path": exact_path,
                    "logical_path": logical_path(exact_path),
                    "extent_lba": record.extent_location(),
                    "bytes": record.data_length,
                }
                if include_file_hashes:
                    extent_end = record.extent_location() * 2048 + record.data_length
                    if extent_end > image_bytes:
                        # XA/interleaved directory records can describe a
                        # logical payload larger than this 2048-byte inventory
                        # view. Keep the record visible but never claim to have
                        # hashed bytes which are not present in the cooked ISO.
                        entry["hash_status"] = "extent-outside-image"
                    else:
                        sink = DigestSink()
                        image.get_file_from_iso_fp(sink, iso_path=exact_path)
                        if sink.bytes != record.data_length:
                            entry["hash_status"] = "extracted-length-mismatch"
                            entry["extracted_bytes"] = sink.bytes
                        else:
                            entry["hash_status"] = "ok"
                            entry["sha256"] = sink.digest.hexdigest()
                            entry["signature"] = (
                                "ps-x-exe"
                                if bytes(sink.prefix) == b"PS-X EXE"
                                else "none"
                            )
                entries.append(entry)
        entries.sort(key=lambda item: item["iso_path"])
        volume = (
            bytes(image.pvd.volume_identifier).rstrip(b" \0").decode("ascii", "replace")
        )
    except (OSError, pycdlib.pycdlibexception.PyCdlibException) as error:
        raise InventoryError(str(error)) from error
    finally:
        image.close()

    signed = [
        item["iso_path"] for item in entries if item.get("signature") == "ps-x-exe"
    ]
    return {
        "schema": "psxrecomp.iso9660-inventory.v1",
        "status": "ok",
        "image": {
            "name": iso_path.name,
            "bytes": image_bytes,
            "sha256": sha256_file(iso_path),
            "logical_block_bytes": 2048,
            "volume_identifier": volume,
        },
        "file_count": len(entries),
        "ps_x_exe_files": signed,
        "files": entries,
    }


def extract_selected(
    iso_path: Path,
    report: dict[str, Any],
    output: Path,
    suffixes: tuple[str, ...],
) -> list[dict[str, Any]]:
    if pycdlib is None:
        raise InventoryError("pycdlib 1.14.0 is required for ISO9660 extraction")
    if output.exists():
        raise InventoryError(f"refusing to overwrite extraction directory: {output}")
    normalized = tuple(sorted({suffix.upper() for suffix in suffixes}))
    if not normalized or any(
        not suffix.startswith(".") or "/" in suffix for suffix in normalized
    ):
        raise InventoryError(
            "each extraction suffix must begin with '.' and contain no slash"
        )
    selected = [
        entry
        for entry in report["files"]
        if str(entry["logical_path"]).upper().endswith(normalized)
    ]
    if not selected:
        raise InventoryError("no ISO files match the requested suffixes")
    for entry in selected:
        if entry.get("hash_status") != "ok":
            raise InventoryError(
                f"selected ISO file is not exactly hashable: {entry['iso_path']}"
            )

    temporary = output.with_name(output.name + ".partial")
    if temporary.exists():
        raise InventoryError(f"refusing to overwrite partial extraction: {temporary}")
    temporary.mkdir(parents=True)
    image = pycdlib.PyCdlib()
    extracted: list[dict[str, Any]] = []
    try:
        image.open(str(iso_path))
        for entry in selected:
            destination = extraction_destination(temporary, entry["logical_path"])
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open("xb") as stream:
                image.get_file_from_iso_fp(stream, iso_path=str(entry["iso_path"]))
            actual_size = destination.stat().st_size
            actual_hash = sha256_file(destination)
            if actual_size != int(entry["bytes"]) or actual_hash != entry["sha256"]:
                raise InventoryError(
                    f"extracted file identity mismatch: {entry['iso_path']}"
                )
            extracted.append(
                {
                    "logical_path": entry["logical_path"],
                    "bytes": actual_size,
                    "sha256": actual_hash,
                }
            )
        image.close()
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary.replace(output)
        return extracted
    except Exception:
        with suppress(Exception):
            image.close()
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--no-file-hashes", action="store_true")
    parser.add_argument("--extract-root", type=Path)
    parser.add_argument(
        "--extract-suffix",
        action="append",
        default=[],
        help="logical filename suffix to extract; repeat for more than one",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.extract_root and args.no_file_hashes:
            raise InventoryError("extraction requires file hashing")
        if bool(args.extract_root) != bool(args.extract_suffix):
            raise InventoryError(
                "--extract-root and --extract-suffix must be used together"
            )
        report = inventory(args.iso, not args.no_file_hashes)
        if args.extract_root:
            files = extract_selected(
                args.iso, report, args.extract_root, tuple(args.extract_suffix)
            )
            report["extraction"] = {
                "root": args.extract_root.name,
                "suffixes": sorted({suffix.upper() for suffix in args.extract_suffix}),
                "file_count": len(files),
                "files": files,
            }
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            if args.json_out.exists():
                raise InventoryError(f"refusing to overwrite receipt: {args.json_out}")
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(payload, encoding="utf-8", newline="\n")
        sys.stdout.write(payload)
        return 0
    except (OSError, InventoryError) as error:
        sys.stderr.write(f"ISO9660 inventory failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
