"""Clean, offline executable-region catalog primitives."""

from .canonical import (
    JSON_SAFE_INTEGER_LIMIT,
    CanonicalizationError,
    canonical_json_bytes,
    canonical_json_text,
    canonical_sha256,
    load_json_file,
    sha256_bytes,
    sha256_file,
)
from .model import (
    AddressRange,
    Catalog,
    DelaySlotOwnership,
    ExecutableVariant,
    MediaFileProvenance,
    PrecompiledArtifact,
    SourceMediaProvenance,
    ValidationError,
)
from .selection import (
    CatalogSelectionError,
    CatalogSelector,
    SelectionRequest,
    SelectionResult,
    UnknownCodeReceipt,
)

__all__ = [
    "AddressRange",
    "CanonicalizationError",
    "Catalog",
    "CatalogSelectionError",
    "CatalogSelector",
    "DelaySlotOwnership",
    "ExecutableVariant",
    "JSON_SAFE_INTEGER_LIMIT",
    "MediaFileProvenance",
    "PrecompiledArtifact",
    "SelectionRequest",
    "SelectionResult",
    "SourceMediaProvenance",
    "UnknownCodeReceipt",
    "ValidationError",
    "canonical_json_bytes",
    "canonical_json_text",
    "canonical_sha256",
    "load_json_file",
    "sha256_bytes",
    "sha256_file",
]
