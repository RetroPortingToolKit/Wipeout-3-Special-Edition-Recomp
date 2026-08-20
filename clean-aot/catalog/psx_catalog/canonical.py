"""Strict canonical JSON and hashing helpers for catalog identity."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Dict, List, Union


class CanonicalizationError(ValueError):
    """Raised when a value cannot be represented by the catalog JSON profile."""


JsonValue = Union[None, bool, int, str, List["JsonValue"], Dict[str, "JsonValue"]]
JSON_SAFE_INTEGER_LIMIT = (1 << 53) - 1


def _reject_constant(value: str) -> Any:
    raise CanonicalizationError(f"non-finite JSON number at $: {value}")


def _reject_float(value: str) -> Any:
    raise CanonicalizationError(f"floating-point JSON number is not allowed at $: {value}")


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise CanonicalizationError(f"duplicate JSON object key at $: {key!r}")
        result[key] = value
    return result


def load_json_text(text: str) -> JsonValue:
    """Parse JSON while rejecting ambiguities that break deterministic identity."""

    try:
        value = json.loads(
            text,
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=_reject_constant,
            parse_float=_reject_float,
        )
    except CanonicalizationError:
        raise
    except (TypeError, ValueError, json.JSONDecodeError) as exc:
        raise CanonicalizationError(f"invalid JSON: {exc}") from exc
    return canonicalize(value)


def load_json_file(path: str | Path) -> JsonValue:
    """Load UTF-8 JSON from *path* under the strict canonical profile."""

    file_path = Path(path)
    try:
        text = file_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise CanonicalizationError(f"cannot read JSON file: {exc}") from exc
    return load_json_text(text)


def _validated_string(value: str, path: str) -> str:
    """Reject strings that cannot have one strict UTF-8 representation."""

    try:
        value.encode("utf-8", errors="strict")
    except UnicodeEncodeError as exc:
        raise CanonicalizationError(f"invalid Unicode scalar value at {path}") from exc
    return value


def canonicalize(value: Any, path: str = "$") -> JsonValue:
    """Return a fresh canonicalizable JSON value or raise a precise error.

    Floats are intentionally rejected instead of rounded. This catalog models
    addresses, sizes, hashes, and opaque relocation facts; accepting a float
    would make two languages disagree about its canonical spelling.
    """

    if value is None or isinstance(value, bool):
        return value
    if isinstance(value, int):
        if not -JSON_SAFE_INTEGER_LIMIT <= value <= JSON_SAFE_INTEGER_LIMIT:
            raise CanonicalizationError(f"integer is outside the interoperable JSON range at {path}")
        return int(value)
    if isinstance(value, str):
        return _validated_string(value, path)
    if isinstance(value, float):
        raise CanonicalizationError(f"floating-point value is not allowed at {path}")
    if isinstance(value, list):
        return [canonicalize(item, f"{path}[{index}]") for index, item in enumerate(value)]
    if isinstance(value, dict):
        if any(not isinstance(key, str) for key in value):
            raise CanonicalizationError(f"object key is not a string at {path}")
        result: dict[str, JsonValue] = {}
        for key in sorted(value):
            checked_key = _validated_string(key, path)
            result[checked_key] = canonicalize(value[key], f"{path}.{checked_key}")
        return result
    raise CanonicalizationError(f"unsupported JSON value at {path}: {type(value).__name__}")


def canonical_json_bytes(value: Any) -> bytes:
    """Serialize *value* using the catalog's canonical JSON profile."""

    normalized = canonicalize(value)
    try:
        text = json.dumps(
            normalized,
            ensure_ascii=False,
            allow_nan=False,
            separators=(",", ":"),
            sort_keys=False,
        )
        return text.encode("utf-8")
    except (TypeError, UnicodeError, ValueError) as exc:
        raise CanonicalizationError(f"cannot serialize canonical JSON: {exc}") from exc


def canonical_json_text(value: Any) -> str:
    """Return canonical JSON with one terminal newline for files/CLI output."""

    return canonical_json_bytes(value).decode("utf-8") + "\n"


def sha256_bytes(data: bytes) -> str:
    """Return a lower-case SHA-256 digest for *data*."""

    return hashlib.sha256(data).hexdigest()


def sha256_file(path: str | Path, chunk_size: int = 1024 * 1024) -> tuple[str, int]:
    """Hash a file without loading it into memory; return ``(digest, size)``."""

    if type(chunk_size) is not int or chunk_size <= 0:
        raise ValueError("chunk_size must be a positive integer")
    digest = hashlib.sha256()
    size = 0
    try:
        with Path(path).open("rb") as stream:
            while True:
                chunk = stream.read(chunk_size)
                if not chunk:
                    break
                digest.update(chunk)
                size += len(chunk)
    except OSError as exc:
        raise OSError(f"cannot hash file: {exc}") from exc
    return digest.hexdigest(), size


def canonical_sha256(value: Any) -> str:
    """Hash the canonical bytes of a JSON-compatible value."""

    return sha256_bytes(canonical_json_bytes(value))
