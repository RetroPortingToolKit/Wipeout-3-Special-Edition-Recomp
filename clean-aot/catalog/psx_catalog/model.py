"""Strict data model for the offline executable-region catalog."""

from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass, field
from typing import Any, Mapping, Optional

from .canonical import (
    JSON_SAFE_INTEGER_LIMIT,
    JsonValue,
    canonical_json_bytes,
    canonical_json_text,
    canonical_sha256,
    canonicalize,
    load_json_text,
)


SCHEMA = "psxrecomp.clean-aot.catalog.v1"
UNKNOWN_RECEIPT_SCHEMA = "psxrecomp.clean-aot.unknown-executable.v1"
SELECTION_SCHEMA = "psxrecomp.clean-aot.selection.v1"
ADDRESS_LIMIT = 1 << 32
INSTRUCTION_ALIGNMENT = 4
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_TOKEN_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]*$")
_WINDOWS_FORBIDDEN = frozenset('<>:"\\|?*')
_WINDOWS_RESERVED = frozenset(
    {"con", "prn", "aux", "nul", "clock$"}
    | {f"com{index}" for index in range(1, 10)}
    | {f"lpt{index}" for index in range(1, 10)}
)


class ValidationError(ValueError):
    """Raised when a catalog or request violates the strict schema."""


def _strict_keys(value: Any, expected: set[str], path: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise ValidationError(f"{path} must be an object")
    actual = set(value)
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing:
        raise ValidationError(f"{path} missing keys: {','.join(missing)}")
    if extra:
        raise ValidationError(f"{path} unknown keys: {','.join(extra)}")
    return value


def _integer(value: Any, path: str, *, minimum: Optional[int] = None) -> int:
    if type(value) is not int:
        raise ValidationError(f"{path} must be an integer")
    if not -JSON_SAFE_INTEGER_LIMIT <= value <= JSON_SAFE_INTEGER_LIMIT:
        raise ValidationError(f"{path} is outside the interoperable JSON range")
    if minimum is not None and value < minimum:
        raise ValidationError(f"{path} must be >= {minimum}")
    return value


def _text(value: Any, path: str, *, token: bool = False) -> str:
    if not isinstance(value, str) or not value or value.strip() != value:
        raise ValidationError(f"{path} must be a non-empty trimmed string")
    if any(ord(character) < 0x20 for character in value):
        raise ValidationError(f"{path} contains a control character")
    try:
        canonicalize(value, path)
    except ValueError as exc:
        raise ValidationError(str(exc)) from exc
    if token and _TOKEN_RE.fullmatch(value) is None:
        raise ValidationError(f"{path} is not a safe identifier")
    return value


def _digest(value: Any, path: str) -> str:
    result = _text(value, path)
    if _SHA256_RE.fullmatch(result) is None:
        raise ValidationError(f"{path} must be lower-case SHA-256 hex")
    return result


def _canonical_object(value: Any, path: str) -> Optional[dict[str, JsonValue]]:
    if value is None:
        return None
    if not isinstance(value, dict):
        raise ValidationError(f"{path} must be an object or null")
    try:
        normalized = canonicalize(value, path)
    except ValueError as exc:
        raise ValidationError(str(exc)) from exc
    assert isinstance(normalized, dict)
    return normalized


def _safe_relative_path(value: Any, path: str) -> str:
    result = _text(value, path)
    if unicodedata.normalize("NFC", result) != result:
        raise ValidationError(f"{path} must use Unicode NFC")
    if result.startswith(("/", "\\")) or ":" in result:
        raise ValidationError(f"{path} must be a relative POSIX path")
    if "\\" in result:
        raise ValidationError(f"{path} must be a normalized relative POSIX path")
    parts = result.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise ValidationError(f"{path} must be a normalized relative POSIX path")
    for part in parts:
        if part != part.strip() or part.endswith((".", " ")):
            raise ValidationError(f"{path} contains a non-portable path component")
        if any(character in _WINDOWS_FORBIDDEN for character in part):
            raise ValidationError(f"{path} contains a Windows-reserved character")
        if any(unicodedata.category(character) in {"Cc", "Cf", "Cs"} for character in part):
            raise ValidationError(f"{path} contains a control or formatting character")
        device_stem = part.split(".", 1)[0].casefold()
        if device_stem in _WINDOWS_RESERVED:
            raise ValidationError(f"{path} contains a Windows-reserved device name")
    return result


def _safe_base_name(value: Any, path: str) -> str:
    result = _safe_relative_path(value, path)
    if "/" in result or result.startswith((".", "~")):
        raise ValidationError(f"{path} must be a portable base name")
    return result


def _portable_path_key(value: str) -> str:
    """Conservative collision key shared by case-sensitive and Windows hosts."""

    return unicodedata.normalize("NFC", value).casefold()


@dataclass(frozen=True)
class AddressRange:
    """A half-open 32-bit guest-address range ``[start, end_exclusive)``."""

    start: int
    end_exclusive: int

    def __post_init__(self) -> None:
        if type(self.start) is not int or type(self.end_exclusive) is not int:
            raise ValidationError("address range endpoints must be integers")
        if not 0 <= self.start < self.end_exclusive <= ADDRESS_LIMIT:
            raise ValidationError("address range must satisfy 0 <= start < end_exclusive <= 2^32")

    @classmethod
    def from_dict(cls, value: Any, path: str = "range") -> "AddressRange":
        fields = _strict_keys(value, {"start", "end_exclusive"}, path)
        return cls(
            _integer(fields["start"], f"{path}.start", minimum=0),
            _integer(fields["end_exclusive"], f"{path}.end_exclusive", minimum=1),
        )

    def to_dict(self) -> dict[str, int]:
        return {"start": self.start, "end_exclusive": self.end_exclusive}

    @property
    def length(self) -> int:
        return self.end_exclusive - self.start

    def contains_address(self, address: int) -> bool:
        return self.start <= address < self.end_exclusive

    def contains_range(self, other: "AddressRange") -> bool:
        return self.start <= other.start and other.end_exclusive <= self.end_exclusive

    def overlaps(self, other: "AddressRange") -> bool:
        return self.start < other.end_exclusive and other.start < self.end_exclusive


@dataclass(frozen=True)
class MediaFileProvenance:
    """Hash-only provenance for one source-media file."""

    role: str
    name: str
    size: int
    sha256: str

    def __post_init__(self) -> None:
        _text(self.role, "media_file.role", token=True)
        _safe_base_name(self.name, "media_file.name")
        _integer(self.size, "media_file.size", minimum=0)
        _digest(self.sha256, "media_file.sha256")

    @classmethod
    def from_dict(cls, value: Any, path: str) -> "MediaFileProvenance":
        fields = _strict_keys(value, {"role", "name", "size", "sha256"}, path)
        return cls(
            _text(fields["role"], f"{path}.role", token=True),
            _safe_base_name(fields["name"], f"{path}.name"),
            _integer(fields["size"], f"{path}.size", minimum=0),
            _digest(fields["sha256"], f"{path}.sha256"),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "role": self.role,
            "name": self.name,
            "size": self.size,
            "sha256": self.sha256,
        }


@dataclass(frozen=True)
class SourceMediaProvenance:
    """Stable, content-addressed provenance without embedding disc data."""

    source_id: str
    format: str
    files: tuple[MediaFileProvenance, ...]

    def __post_init__(self) -> None:
        _text(self.source_id, "source_media.source_id", token=True)
        _text(self.format, "source_media.format", token=True)
        if not isinstance(self.files, tuple) or not self.files:
            raise ValidationError("source_media.files must not be empty")
        if any(not isinstance(item, MediaFileProvenance) for item in self.files):
            raise ValidationError("source_media.files contains an invalid type")
        if tuple(sorted(self.files, key=lambda item: (item.role, item.name))) != self.files:
            raise ValidationError("source_media.files must be sorted by role and name")
        names = [(item.role.casefold(), _portable_path_key(item.name)) for item in self.files]
        if len(names) != len(set(names)):
            raise ValidationError("source_media.files must not contain portable-path duplicates")

    @classmethod
    def from_dict(cls, value: Any, path: str = "source_media") -> "SourceMediaProvenance":
        fields = _strict_keys(value, {"source_id", "format", "files"}, path)
        raw_files = fields["files"]
        if not isinstance(raw_files, list):
            raise ValidationError(f"{path}.files must be an array")
        files = tuple(
            MediaFileProvenance.from_dict(item, f"{path}.files[{index}]")
            for index, item in enumerate(raw_files)
        )
        return cls(
            _text(fields["source_id"], f"{path}.source_id", token=True),
            _text(fields["format"], f"{path}.format", token=True),
            files,
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "source_id": self.source_id,
            "format": self.format,
            "files": [item.to_dict() for item in self.files],
        }


@dataclass(frozen=True)
class PrecompiledArtifact:
    """The only executable target a catalog selection may return."""

    id: str
    path: str
    sha256: str

    def __post_init__(self) -> None:
        _text(self.id, "artifact.id", token=True)
        _safe_relative_path(self.path, "artifact.path")
        _digest(self.sha256, "artifact.sha256")

    @classmethod
    def from_dict(cls, value: Any, path: str = "artifact") -> "PrecompiledArtifact":
        fields = _strict_keys(value, {"id", "path", "sha256"}, path)
        return cls(
            _text(fields["id"], f"{path}.id", token=True),
            _safe_relative_path(fields["path"], f"{path}.path"),
            _digest(fields["sha256"], f"{path}.sha256"),
        )

    def to_dict(self) -> dict[str, str]:
        return {"id": self.id, "path": self.path, "sha256": self.sha256}


@dataclass(frozen=True)
class DelaySlotOwnership:
    """A branch and the instruction it owns as its architected delay slot."""

    branch_pc: int
    delay_slot_pc: int
    owner_root: int

    def __post_init__(self) -> None:
        for name, value in (
            ("branch_pc", self.branch_pc),
            ("delay_slot_pc", self.delay_slot_pc),
            ("owner_root", self.owner_root),
        ):
            _integer(value, f"delay_slot.{name}", minimum=0)
            if value >= ADDRESS_LIMIT:
                raise ValidationError(f"delay_slot.{name} must be a 32-bit guest address")
            if value % INSTRUCTION_ALIGNMENT:
                raise ValidationError(f"delay_slot.{name} must be 4-byte aligned")
        if self.delay_slot_pc != self.branch_pc + INSTRUCTION_ALIGNMENT:
            raise ValidationError("delay_slot must be the instruction immediately after branch")

    @classmethod
    def from_dict(cls, value: Any, path: str) -> "DelaySlotOwnership":
        fields = _strict_keys(value, {"branch_pc", "delay_slot_pc", "owner_root"}, path)
        return cls(
            _integer(fields["branch_pc"], f"{path}.branch_pc", minimum=0),
            _integer(fields["delay_slot_pc"], f"{path}.delay_slot_pc", minimum=0),
            _integer(fields["owner_root"], f"{path}.owner_root", minimum=0),
        )

    def to_dict(self) -> dict[str, int]:
        return {
            "branch_pc": self.branch_pc,
            "delay_slot_pc": self.delay_slot_pc,
            "owner_root": self.owner_root,
        }

    def crosses_page(self, page_size: int) -> bool:
        if type(page_size) is not int or page_size < INSTRUCTION_ALIGNMENT or page_size & (page_size - 1):
            raise ValidationError("page_size must be a power of two of at least 4 bytes")
        return self.branch_pc // page_size != self.delay_slot_pc // page_size


@dataclass(frozen=True)
class ExecutableVariant:
    """One immutable content epoch and its precompiled artifact."""

    load_range: AddressRange
    sha256: str
    relocation_metadata: Optional[dict[str, JsonValue]]
    dispatch_roots: tuple[int, ...]
    executed_ranges: tuple[AddressRange, ...]
    delay_slot_owners: tuple[DelaySlotOwnership, ...]
    source_media: SourceMediaProvenance
    artifact: PrecompiledArtifact
    _relocation_metadata_bytes: bytes = field(init=False, repr=False, compare=False)

    def __post_init__(self) -> None:
        if not isinstance(self.load_range, AddressRange):
            raise ValidationError("variant.load_range has an invalid type")
        if not isinstance(self.source_media, SourceMediaProvenance):
            raise ValidationError("variant.source_media has an invalid type")
        if not isinstance(self.artifact, PrecompiledArtifact):
            raise ValidationError("variant.artifact has an invalid type")
        if self.load_range.start % INSTRUCTION_ALIGNMENT or self.load_range.end_exclusive % INSTRUCTION_ALIGNMENT:
            raise ValidationError("variant.load_range must be 4-byte aligned")
        _digest(self.sha256, "variant.sha256")
        normalized_metadata = _canonical_object(self.relocation_metadata, "variant.relocation_metadata")
        object.__setattr__(self, "relocation_metadata", normalized_metadata)
        object.__setattr__(
            self,
            "_relocation_metadata_bytes",
            canonical_json_bytes(normalized_metadata),
        )
        if not isinstance(self.dispatch_roots, tuple) or not self.dispatch_roots:
            raise ValidationError("variant.dispatch_roots must not be empty")
        if tuple(sorted(self.dispatch_roots)) != self.dispatch_roots:
            raise ValidationError("variant.dispatch_roots must be sorted")
        if len(set(self.dispatch_roots)) != len(self.dispatch_roots):
            raise ValidationError("variant.dispatch_roots must not contain duplicates")
        for root in self.dispatch_roots:
            _integer(root, "variant.dispatch_root", minimum=0)
            if root >= ADDRESS_LIMIT:
                raise ValidationError("variant.dispatch_roots must be 32-bit guest addresses")
            if root % INSTRUCTION_ALIGNMENT:
                raise ValidationError("variant.dispatch_roots must be 4-byte aligned")
            if not self.load_range.contains_address(root):
                raise ValidationError("variant.dispatch_root must be inside load_range")
        if not isinstance(self.executed_ranges, tuple) or not self.executed_ranges:
            raise ValidationError("variant.executed_ranges must not be empty")
        if any(not isinstance(executed, AddressRange) for executed in self.executed_ranges):
            raise ValidationError("variant.executed_ranges contains an invalid type")
        previous: Optional[AddressRange] = None
        for executed in self.executed_ranges:
            if executed.start % INSTRUCTION_ALIGNMENT or executed.end_exclusive % INSTRUCTION_ALIGNMENT:
                raise ValidationError("variant.executed_ranges must be 4-byte aligned")
            if not self.load_range.contains_range(executed):
                raise ValidationError("variant.executed_range must be inside load_range")
            if previous is not None:
                if executed.start < previous.end_exclusive:
                    raise ValidationError("variant.executed_ranges must not overlap")
                if executed.start == previous.end_exclusive:
                    raise ValidationError("variant.executed_ranges must be coalesced")
            previous = executed
        for root in self.dispatch_roots:
            if not any(executed.contains_address(root) for executed in self.executed_ranges):
                raise ValidationError("variant.dispatch_root must be inside executed_ranges")
        if not isinstance(self.delay_slot_owners, tuple) or any(
            not isinstance(owner, DelaySlotOwnership) for owner in self.delay_slot_owners
        ):
            raise ValidationError("variant.delay_slot_owners contains an invalid type")
        if tuple(sorted(self.delay_slot_owners, key=lambda item: item.branch_pc)) != self.delay_slot_owners:
            raise ValidationError("variant.delay_slot_owners must be sorted by branch_pc")
        branches = [item.branch_pc for item in self.delay_slot_owners]
        if len(branches) != len(set(branches)):
            raise ValidationError("variant.delay_slot_owners must not contain duplicate branches")
        for owner in self.delay_slot_owners:
            if owner.owner_root not in self.dispatch_roots:
                raise ValidationError("delay_slot.owner_root must be a dispatch root")
            if owner.owner_root > owner.branch_pc:
                raise ValidationError("delay_slot.owner_root must not follow its branch")
            if not self.load_range.contains_address(owner.branch_pc) or not self.load_range.contains_address(owner.delay_slot_pc):
                raise ValidationError("delay-slot instructions must be inside load_range")
            if not any(
                executed.contains_address(owner.branch_pc)
                and executed.contains_address(owner.delay_slot_pc)
                for executed in self.executed_ranges
            ):
                raise ValidationError("branch and delay slot must share an executed range")

    @classmethod
    def from_dict(cls, value: Any, path: str = "variant") -> "ExecutableVariant":
        fields = _strict_keys(
            value,
            {
                "load_range",
                "sha256",
                "relocation_metadata",
                "dispatch_roots",
                "executed_ranges",
                "delay_slot_owners",
                "source_media",
                "artifact",
            },
            path,
        )
        roots = fields["dispatch_roots"]
        ranges = fields["executed_ranges"]
        owners = fields["delay_slot_owners"]
        if not isinstance(roots, list) or not isinstance(ranges, list) or not isinstance(owners, list):
            raise ValidationError(f"{path} roots, ranges, and delay slots must be arrays")
        return cls(
            AddressRange.from_dict(fields["load_range"], f"{path}.load_range"),
            _digest(fields["sha256"], f"{path}.sha256"),
            _canonical_object(fields["relocation_metadata"], f"{path}.relocation_metadata"),
            tuple(_integer(root, f"{path}.dispatch_roots[{index}]", minimum=0) for index, root in enumerate(roots)),
            tuple(
                AddressRange.from_dict(item, f"{path}.executed_ranges[{index}]")
                for index, item in enumerate(ranges)
            ),
            tuple(
                DelaySlotOwnership.from_dict(item, f"{path}.delay_slot_owners[{index}]")
                for index, item in enumerate(owners)
            ),
            SourceMediaProvenance.from_dict(fields["source_media"], f"{path}.source_media"),
            PrecompiledArtifact.from_dict(fields["artifact"], f"{path}.artifact"),
        )

    def identity_dict(self) -> dict[str, Any]:
        return {
            "load_range": self.load_range.to_dict(),
            "sha256": self.sha256,
            "relocation_metadata": self._relocation_metadata_copy(),
        }

    def _relocation_metadata_copy(self) -> Optional[dict[str, JsonValue]]:
        value = load_json_text(self._relocation_metadata_bytes.decode("utf-8"))
        assert value is None or isinstance(value, dict)
        return value

    @property
    def identity_sha256(self) -> str:
        return canonical_sha256(self.identity_dict())

    def to_dict(self) -> dict[str, Any]:
        return {
            "load_range": self.load_range.to_dict(),
            "sha256": self.sha256,
            "relocation_metadata": self._relocation_metadata_copy(),
            "dispatch_roots": list(self.dispatch_roots),
            "executed_ranges": [item.to_dict() for item in self.executed_ranges],
            "delay_slot_owners": [item.to_dict() for item in self.delay_slot_owners],
            "source_media": self.source_media.to_dict(),
            "artifact": self.artifact.to_dict(),
        }

    @property
    def data_ranges(self) -> tuple[AddressRange, ...]:
        """Return load bytes that are deliberately not classified as code."""

        result: list[AddressRange] = []
        cursor = self.load_range.start
        for executed in self.executed_ranges:
            if cursor < executed.start:
                result.append(AddressRange(cursor, executed.start))
            cursor = executed.end_exclusive
        if cursor < self.load_range.end_exclusive:
            result.append(AddressRange(cursor, self.load_range.end_exclusive))
        return tuple(result)

    def is_executable_address(self, address: int) -> bool:
        return any(executed.contains_address(address) for executed in self.executed_ranges)


@dataclass(frozen=True)
class Catalog:
    """A validated set of immutable executable variants."""

    catalog_id: str
    stack_window: AddressRange
    page_size: int
    variants: tuple[ExecutableVariant, ...]
    schema: str = SCHEMA
    address_bits: int = 32

    def __post_init__(self) -> None:
        if not isinstance(self.stack_window, AddressRange):
            raise ValidationError("catalog.stack_window has an invalid type")
        if self.schema != SCHEMA:
            raise ValidationError(f"catalog.schema must equal {SCHEMA}")
        if self.address_bits != 32:
            raise ValidationError("catalog.address_bits must equal 32")
        _text(self.catalog_id, "catalog.catalog_id", token=True)
        if type(self.page_size) is not int or self.page_size < 4 or self.page_size > 1 << 20:
            raise ValidationError("catalog.page_size must be an integer between 4 and 2^20")
        if self.page_size & (self.page_size - 1):
            raise ValidationError("catalog.page_size must be a power of two")
        if self.stack_window.start % INSTRUCTION_ALIGNMENT or self.stack_window.end_exclusive % INSTRUCTION_ALIGNMENT:
            raise ValidationError("catalog.stack_window must be 4-byte aligned")
        if not isinstance(self.variants, tuple):
            raise ValidationError("catalog.variants must be a tuple")
        if any(not isinstance(variant, ExecutableVariant) for variant in self.variants):
            raise ValidationError("catalog.variants contains an invalid type")
        variant_identity_order = [variant.identity_sha256 for variant in self.variants]
        if variant_identity_order != sorted(variant_identity_order):
            raise ValidationError("catalog.variants must be sorted by executable identity")
        identities: set[str] = set()
        artifact_ids: set[str] = set()
        artifact_paths: set[str] = set()
        for variant in self.variants:
            if variant.load_range.overlaps(self.stack_window):
                raise ValidationError("variant.load_range overlaps catalog.stack_window")
            identity = variant.identity_sha256
            if identity in identities:
                raise ValidationError("catalog.variants contains a duplicate executable identity")
            identities.add(identity)
            if variant.artifact.id in artifact_ids:
                raise ValidationError("catalog artifacts must have unique IDs")
            portable_path = _portable_path_key(variant.artifact.path)
            if portable_path in artifact_paths:
                raise ValidationError("catalog artifacts must have unique portable paths")
            artifact_ids.add(variant.artifact.id)
            artifact_paths.add(portable_path)

    @classmethod
    def from_dict(cls, value: Any) -> "Catalog":
        fields = _strict_keys(
            value,
            {"schema", "catalog_id", "address_bits", "page_size", "stack_window", "variants"},
            "catalog",
        )
        raw_variants = fields["variants"]
        if not isinstance(raw_variants, list):
            raise ValidationError("catalog.variants must be an array")
        return cls(
            _text(fields["catalog_id"], "catalog.catalog_id", token=True),
            AddressRange.from_dict(fields["stack_window"], "catalog.stack_window"),
            _integer(fields["page_size"], "catalog.page_size", minimum=4),
            tuple(
                ExecutableVariant.from_dict(item, f"catalog.variants[{index}]")
                for index, item in enumerate(raw_variants)
            ),
            _text(fields["schema"], "catalog.schema"),
            _integer(fields["address_bits"], "catalog.address_bits", minimum=1),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema": self.schema,
            "catalog_id": self.catalog_id,
            "address_bits": self.address_bits,
            "page_size": self.page_size,
            "stack_window": self.stack_window.to_dict(),
            "variants": [item.to_dict() for item in self.variants],
        }

    def canonical_json(self) -> str:
        return canonical_json_text(self.to_dict())

    @property
    def sha256(self) -> str:
        return canonical_sha256(self.to_dict())

    def variant_for_identity(
        self,
        load_range: AddressRange,
        sha256: str,
        relocation_metadata: Optional[dict[str, JsonValue]],
    ) -> Optional[ExecutableVariant]:
        normalized_metadata = _canonical_object(relocation_metadata, "identity.relocation_metadata")
        identity = canonical_sha256(
            {
                "load_range": load_range.to_dict(),
                "sha256": _digest(sha256, "identity.sha256"),
                "relocation_metadata": normalized_metadata,
            }
        )
        for variant in self.variants:
            if variant.identity_sha256 == identity and variant.identity_dict() == {
                "load_range": load_range.to_dict(),
                "sha256": sha256,
                "relocation_metadata": normalized_metadata,
            }:
                return variant
        return None
