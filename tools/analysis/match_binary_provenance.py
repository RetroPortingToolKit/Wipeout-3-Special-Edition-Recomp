#!/usr/bin/env python3
"""Find exact byte runs shared by captured blobs and authenticated sources.

The receipt proves byte equality only.  It does not infer a load transform,
code boundaries, executable coverage, or semantic equivalence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

OUTPUT_SCHEMA = "wo3se.binary-provenance-match.v1"
DEFAULT_ANCHOR_BYTES = 32
DEFAULT_MIN_MATCH_BYTES = 64
MAX_FILES = 8192
MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_TOTAL_BYTES = 512 * 1024 * 1024
MAX_ANCHOR_OCCURRENCES = 128
MAX_CANDIDATE_COMPARISONS = 10_000_000
MAX_MATCHES = 1_000_000


class ProvenanceMatchError(RuntimeError):
    pass


@dataclass(frozen=True)
class Blob:
    label: str
    payload: bytes
    sha256: str


@dataclass(frozen=True, order=True)
class Match:
    query_index: int
    source_index: int
    query_offset: int
    source_offset: int
    length: int


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _validate_blobs(blobs: list[Blob], role: str) -> None:
    if not blobs:
        raise ProvenanceMatchError(f"at least one {role} blob is required")
    if len(blobs) > MAX_FILES:
        raise ProvenanceMatchError(f"too many {role} blobs")
    total = 0
    labels: set[str] = set()
    for blob in blobs:
        if not blob.label or blob.label in labels:
            raise ProvenanceMatchError(f"{role} labels must be non-empty and unique")
        labels.add(blob.label)
        if not blob.payload:
            raise ProvenanceMatchError(f"empty {role} blob: {blob.label}")
        if len(blob.payload) > MAX_FILE_BYTES:
            raise ProvenanceMatchError(f"{role} blob exceeds size limit: {blob.label}")
        if blob.sha256 != _sha256(blob.payload):
            raise ProvenanceMatchError(f"incorrect {role} digest: {blob.label}")
        total += len(blob.payload)
        if total > MAX_TOTAL_BYTES:
            raise ProvenanceMatchError(f"total {role} bytes exceed size limit")


def _extend_match(
    query: bytes,
    source: bytes,
    query_offset: int,
    source_offset: int,
    anchor_bytes: int,
) -> tuple[int, int, int]:
    left = 0
    while (
        query_offset > left
        and source_offset > left
        and query[query_offset - left - 1] == source[source_offset - left - 1]
    ):
        left += 1
    right = anchor_bytes
    while (
        query_offset + right < len(query)
        and source_offset + right < len(source)
        and query[query_offset + right] == source[source_offset + right]
    ):
        right += 1
    return query_offset - left, source_offset - left, left + right


def _merge_matches(matches: list[Match]) -> list[Match]:
    ordered = sorted(
        matches,
        key=lambda item: (
            item.query_index,
            item.source_index,
            item.query_offset - item.source_offset,
            item.query_offset,
            item.source_offset,
            -item.length,
        ),
    )
    merged: list[Match] = []
    for item in ordered:
        if merged:
            previous = merged[-1]
            same_diagonal = (
                previous.query_index == item.query_index
                and previous.source_index == item.source_index
                and previous.query_offset - previous.source_offset
                == item.query_offset - item.source_offset
            )
            previous_end = previous.query_offset + previous.length
            item_end = item.query_offset + item.length
            if same_diagonal and item.query_offset <= previous_end:
                if item_end > previous_end:
                    merged[-1] = Match(
                        previous.query_index,
                        previous.source_index,
                        previous.query_offset,
                        previous.source_offset,
                        item_end - previous.query_offset,
                    )
                continue
        merged.append(item)
    return sorted(merged)


def _union_size(intervals: list[tuple[int, int]]) -> int:
    if not intervals:
        return 0
    ordered = sorted(intervals)
    merged = 0
    start, end = ordered[0]
    for next_start, next_end in ordered[1:]:
        if next_start <= end:
            end = max(end, next_end)
        else:
            merged += end - start
            start, end = next_start, next_end
    return merged + end - start


def match_provenance(
    queries: list[Blob],
    sources: list[Blob],
    *,
    anchor_bytes: int = DEFAULT_ANCHOR_BYTES,
    min_match_bytes: int = DEFAULT_MIN_MATCH_BYTES,
) -> dict[str, Any]:
    _validate_blobs(queries, "query")
    _validate_blobs(sources, "source")
    if anchor_bytes < 8 or anchor_bytes > 256:
        raise ProvenanceMatchError("anchor size must be between 8 and 256 bytes")
    if min_match_bytes < anchor_bytes or min_match_bytes > MAX_FILE_BYTES:
        raise ProvenanceMatchError("minimum match must cover at least one anchor")

    anchor_index: dict[bytes, list[tuple[int, int]] | None] = {}
    suppressed_anchor_count = 0
    for query_index, query in enumerate(queries):
        payload = query.payload
        if len(payload) < anchor_bytes:
            continue
        for offset in range(len(payload) - anchor_bytes + 1):
            anchor = payload[offset : offset + anchor_bytes]
            occurrences = anchor_index.get(anchor)
            if occurrences is None and anchor in anchor_index:
                continue
            if occurrences is None:
                anchor_index[anchor] = [(query_index, offset)]
            elif len(occurrences) < MAX_ANCHOR_OCCURRENCES:
                occurrences.append((query_index, offset))
            else:
                anchor_index[anchor] = None
                suppressed_anchor_count += 1

    raw_matches: list[Match] = []
    comparisons = 0
    covered: dict[tuple[int, int, int], list[tuple[int, int]]] = {}
    for source_index, source in enumerate(sources):
        payload = source.payload
        if len(payload) < anchor_bytes:
            continue
        for source_offset in range(len(payload) - anchor_bytes + 1):
            occurrences = anchor_index.get(
                payload[source_offset : source_offset + anchor_bytes]
            )
            if occurrences is None:
                continue
            for query_index, query_offset in occurrences:
                comparisons += 1
                if comparisons > MAX_CANDIDATE_COMPARISONS:
                    raise ProvenanceMatchError("candidate comparison limit exceeded")
                diagonal = query_offset - source_offset
                coverage_key = (query_index, source_index, diagonal)
                if any(
                    start <= source_offset < end
                    for start, end in covered.get(coverage_key, [])
                ):
                    continue
                query_start, source_start, length = _extend_match(
                    queries[query_index].payload,
                    payload,
                    query_offset,
                    source_offset,
                    anchor_bytes,
                )
                if length < min_match_bytes:
                    continue
                raw_matches.append(
                    Match(
                        query_index,
                        source_index,
                        query_start,
                        source_start,
                        length,
                    )
                )
                covered.setdefault(coverage_key, []).append(
                    (source_start, source_start + length)
                )
                if len(raw_matches) > MAX_MATCHES:
                    raise ProvenanceMatchError("match count limit exceeded")

    matches = _merge_matches(raw_matches)
    match_receipts: list[dict[str, Any]] = []
    for item in matches:
        query = queries[item.query_index]
        source = sources[item.source_index]
        payload = query.payload[item.query_offset : item.query_offset + item.length]
        match_receipts.append(
            {
                "query": query.label,
                "source": source.label,
                "query_offset": item.query_offset,
                "source_offset": item.source_offset,
                "length": item.length,
                "sha256": _sha256(payload),
                "whole_query": item.query_offset == 0 and item.length == len(query.payload),
            }
        )

    query_receipts: list[dict[str, Any]] = []
    for query_index, query in enumerate(queries):
        query_matches = [item for item in matches if item.query_index == query_index]
        matched_bytes = _union_size(
            [
                (item.query_offset, item.query_offset + item.length)
                for item in query_matches
            ]
        )
        whole_sources = sorted(
            sources[item.source_index].label
            for item in query_matches
            if item.query_offset == 0 and item.length == len(query.payload)
        )
        query_receipts.append(
            {
                "label": query.label,
                "size": len(query.payload),
                "sha256": query.sha256,
                "status": (
                    "whole-source-match"
                    if whole_sources
                    else "partial-source-match"
                    if matched_bytes
                    else "unmatched"
                ),
                "matched_byte_union": matched_bytes,
                "coverage_ratio": f"{matched_bytes}/{len(query.payload)}",
                "longest_match": max((item.length for item in query_matches), default=0),
                "whole_match_sources": whole_sources,
            }
        )

    return {
        "schema": OUTPUT_SCHEMA,
        "status": "ok",
        "classification": (
            "exact shared-byte runs only; no load-transform, code/data, semantic, "
            "or executable-coverage claim"
        ),
        "parameters": {
            "anchor_bytes": anchor_bytes,
            "minimum_match_bytes": min_match_bytes,
            "suppressed_repetitive_anchor_count": suppressed_anchor_count,
            "candidate_comparisons": comparisons,
        },
        "queries": query_receipts,
        "sources": [
            {"label": source.label, "size": len(source.payload), "sha256": source.sha256}
            for source in sources
        ],
        "match_count": len(match_receipts),
        "matches": match_receipts,
    }


def _collect_paths(inputs: list[Path], suffixes: set[str]) -> list[Path]:
    collected: list[Path] = []
    for item in inputs:
        if item.is_symlink():
            raise ProvenanceMatchError(f"symbolic-link input is not accepted: {item}")
        if item.is_file():
            candidates = [item]
        elif item.is_dir():
            candidates = [path for path in item.rglob("*") if path.is_file()]
        else:
            raise ProvenanceMatchError(f"missing input: {item}")
        for candidate in candidates:
            if candidate.is_symlink():
                raise ProvenanceMatchError(
                    f"symbolic-link input is not accepted: {candidate}"
                )
            if suffixes and candidate.suffix.lower() not in suffixes:
                continue
            collected.append(candidate.resolve())
    unique = sorted({os.path.normcase(os.fspath(path)): path for path in collected}.values())
    if len(unique) > MAX_FILES:
        raise ProvenanceMatchError("input expansion exceeds the file-count limit")
    return unique


def _label(path: Path, root: Path | None) -> str:
    if root is None:
        return path.name
    try:
        return path.relative_to(root).as_posix()
    except ValueError as error:
        raise ProvenanceMatchError(f"input lies outside receipt root: {path}") from error


def _read_blobs(paths: list[Path], root: Path | None) -> list[Blob]:
    blobs: list[Blob] = []
    for path in paths:
        size = path.stat().st_size
        if size <= 0 or size > MAX_FILE_BYTES:
            raise ProvenanceMatchError(f"input size is outside bounds: {path}")
        payload = path.read_bytes()
        blobs.append(Blob(_label(path, root), payload, _sha256(payload)))
    return blobs


def _write_new(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
    except FileExistsError as error:
        raise ProvenanceMatchError(f"refusing to overwrite output: {path}") from error


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--query", action="append", type=Path, required=True)
    parser.add_argument("--source", action="append", type=Path, required=True)
    parser.add_argument(
        "--include-suffix",
        action="append",
        default=[],
        help="optional case-insensitive suffix filter, for example .bin or .dec",
    )
    parser.add_argument("--root", type=Path, help="root for stable receipt labels")
    parser.add_argument("--anchor-bytes", type=int, default=DEFAULT_ANCHOR_BYTES)
    parser.add_argument("--minimum-match-bytes", type=int, default=DEFAULT_MIN_MATCH_BYTES)
    parser.add_argument("--json-out", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        root = args.root.resolve() if args.root else None
        suffixes = {
            suffix.lower() if suffix.startswith(".") else f".{suffix.lower()}"
            for suffix in args.include_suffix
        }
        query_paths = _collect_paths(args.query, suffixes)
        source_paths = _collect_paths(args.source, suffixes)
        report = match_provenance(
            _read_blobs(query_paths, root),
            _read_blobs(source_paths, root),
            anchor_bytes=args.anchor_bytes,
            min_match_bytes=args.minimum_match_bytes,
        )
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            _write_new(args.json_out, payload)
        sys.stdout.write(payload)
        return 0
    except (OSError, ValueError, ProvenanceMatchError) as error:
        sys.stderr.write(f"binary-provenance matching failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
