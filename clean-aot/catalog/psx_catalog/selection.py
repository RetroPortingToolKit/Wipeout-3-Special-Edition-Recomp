"""Fail-closed exact selection of measured executable and native bytes."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, NoReturn, Optional

from .canonical import (
    JsonValue,
    canonical_json_bytes,
    canonical_json_text,
    canonical_sha256,
    load_json_text,
    sha256_bytes,
    sha256_file,
)
from .model import (
    ADDRESS_LIMIT,
    INSTRUCTION_ALIGNMENT,
    AddressRange,
    Catalog,
    ExecutableVariant,
    SELECTION_SCHEMA,
    UNKNOWN_RECEIPT_SCHEMA,
    ValidationError,
    _canonical_object,
    _digest,
    _integer,
    _strict_keys,
    _text,
)


def _executable_bytes(value: Any) -> bytes:
    if not isinstance(value, (bytes, bytearray, memoryview)):
        raise ValidationError("executable bytes must be a bytes-like object")
    return bytes(value)


@dataclass(frozen=True)
class SelectionRequest:
    """An exact identity query that must also be proved from executable bytes."""

    load_range: AddressRange
    sha256: str
    relocation_metadata: Optional[dict[str, JsonValue]]
    _relocation_metadata_bytes: bytes = field(init=False, repr=False, compare=False)

    def __post_init__(self) -> None:
        if not isinstance(self.load_range, AddressRange):
            raise ValidationError("request.load_range has an invalid type")
        if (
            self.load_range.start % INSTRUCTION_ALIGNMENT
            or self.load_range.end_exclusive % INSTRUCTION_ALIGNMENT
        ):
            raise ValidationError("request.load_range must be 4-byte aligned")
        _digest(self.sha256, "request.sha256")
        normalized = _canonical_object(self.relocation_metadata, "request.relocation_metadata")
        object.__setattr__(self, "relocation_metadata", normalized)
        object.__setattr__(self, "_relocation_metadata_bytes", canonical_json_bytes(normalized))

    @classmethod
    def from_dict(cls, value: Any) -> "SelectionRequest":
        fields = _strict_keys(
            value,
            {"load_range", "sha256", "relocation_metadata"},
            "request",
        )
        return cls(
            AddressRange.from_dict(fields["load_range"], "request.load_range"),
            _digest(fields["sha256"], "request.sha256"),
            _canonical_object(fields["relocation_metadata"], "request.relocation_metadata"),
        )

    @classmethod
    def from_executable_bytes(
        cls,
        load_address: int,
        executable: bytes | bytearray | memoryview,
        relocation_metadata: Optional[dict[str, JsonValue]],
    ) -> "SelectionRequest":
        if type(load_address) is not int or not 0 <= load_address < ADDRESS_LIMIT:
            raise ValidationError("load_address must be a 32-bit guest address")
        payload = _executable_bytes(executable)
        end_exclusive = load_address + len(payload)
        return cls(
            AddressRange(load_address, end_exclusive),
            sha256_bytes(payload),
            relocation_metadata,
        )

    def _relocation_metadata_copy(self) -> Optional[dict[str, JsonValue]]:
        value = load_json_text(self._relocation_metadata_bytes.decode("utf-8"))
        assert value is None or isinstance(value, dict)
        return value

    def to_dict(self) -> dict[str, Any]:
        return {
            "load_range": self.load_range.to_dict(),
            "sha256": self.sha256,
            "relocation_metadata": self._relocation_metadata_copy(),
        }

    @property
    def identity_sha256(self) -> str:
        return canonical_sha256(self.to_dict())


@dataclass(frozen=True)
class SelectionResult:
    """A successful selection whose executable and artifact bytes were verified."""

    catalog_sha256: str
    variant: ExecutableVariant
    artifact_size: int

    def __post_init__(self) -> None:
        _digest(self.catalog_sha256, "selection.catalog_sha256")
        if not isinstance(self.variant, ExecutableVariant):
            raise ValidationError("selection.variant has an invalid type")
        _integer(self.artifact_size, "selection.artifact_size", minimum=0)

    def to_dict(self) -> dict[str, Any]:
        artifact = self.variant.artifact.to_dict()
        artifact["size"] = self.artifact_size
        return {
            "schema": SELECTION_SCHEMA,
            "status": "selected",
            "catalog_sha256": self.catalog_sha256,
            "variant_identity_sha256": self.variant.identity_sha256,
            "artifact": artifact,
        }

    def canonical_json(self) -> str:
        return canonical_json_text(self.to_dict())


@dataclass(frozen=True)
class UnknownCodeReceipt:
    """Small deterministic receipt for every fail-closed selection rejection."""

    catalog_sha256: str
    request: SelectionRequest
    reason: str

    def __post_init__(self) -> None:
        _digest(self.catalog_sha256, "rejection.catalog_sha256")
        if not isinstance(self.request, SelectionRequest):
            raise ValidationError("rejection.request has an invalid type")
        _text(self.reason, "rejection.reason", token=True)

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema": UNKNOWN_RECEIPT_SCHEMA,
            "status": "rejected",
            "reason": self.reason,
            "catalog_sha256": self.catalog_sha256,
            "request": self.request.to_dict(),
        }

    def canonical_json(self) -> str:
        return canonical_json_text(self.to_dict())


class CatalogSelectionError(ValidationError):
    """Raised for every non-successful selection, carrying a compact receipt."""

    def __init__(self, receipt: UnknownCodeReceipt):
        self.receipt = receipt
        super().__init__(receipt.reason)


class CatalogSelector:
    """Select only exact AOT identities with verified executable and artifact bytes.

    There is deliberately no operation that compiles, captures, interprets, or
    resolves an approximate identity. Any measurement or integrity mismatch
    produces :class:`CatalogSelectionError` before an artifact is returned.
    """

    def __init__(self, catalog: Catalog, artifact_root: str | Path):
        if not isinstance(catalog, Catalog):
            raise ValidationError("selector catalog has an invalid type")
        try:
            root = Path(artifact_root).absolute()
        except (OSError, TypeError) as exc:
            raise ValidationError("artifact_root must be a filesystem path") from exc
        self.catalog = catalog
        self.artifact_root = root

    def _reject(self, request: SelectionRequest, reason: str) -> NoReturn:
        raise CatalogSelectionError(UnknownCodeReceipt(self.catalog.sha256, request, reason))

    def _check_request_boundary(self, request: SelectionRequest) -> None:
        if not isinstance(request, SelectionRequest):
            raise ValidationError("selection request has an invalid type")
        if request.load_range.overlaps(self.catalog.stack_window):
            self._reject(request, "request-overlaps-stack-window")

    def select(
        self,
        request: SelectionRequest,
        executable: bytes | bytearray | memoryview,
    ) -> SelectionResult:
        """Measure in-memory executable bytes and select an exact native artifact."""

        self._check_request_boundary(request)
        payload = _executable_bytes(executable)
        if len(payload) != request.load_range.length:
            self._reject(request, "request-executable-size-mismatch")
        if sha256_bytes(payload) != request.sha256:
            self._reject(request, "request-executable-hash-mismatch")
        return self._select_measured(request)

    def select_file(self, request: SelectionRequest, executable_path: str | Path) -> SelectionResult:
        """Stream-hash an executable file and select an exact native artifact."""

        self._check_request_boundary(request)
        digest, size = sha256_file(executable_path)
        if size != request.load_range.length:
            self._reject(request, "request-executable-size-mismatch")
        if digest != request.sha256:
            self._reject(request, "request-executable-hash-mismatch")
        return self._select_measured(request)

    def _select_measured(self, request: SelectionRequest) -> SelectionResult:
        variant = self.catalog.variant_for_identity(
            request.load_range,
            request.sha256,
            request._relocation_metadata_copy(),
        )
        if variant is None:
            self._reject(request, "unknown-executable-variant")
        assert variant is not None
        artifact_size = self._verify_artifact(request, variant)
        return SelectionResult(self.catalog.sha256, variant, artifact_size)

    @staticmethod
    def _is_link_or_junction(path: Path) -> bool:
        if path.is_symlink():
            return True
        is_junction = getattr(path, "is_junction", None)
        return bool(is_junction is not None and is_junction())

    def _verify_artifact(self, request: SelectionRequest, variant: ExecutableVariant) -> int:
        try:
            root = self.artifact_root.resolve(strict=True)
            if not root.is_dir():
                self._reject(request, "native-artifact-root-unavailable")
            candidate = root
            for component in variant.artifact.path.split("/"):
                candidate = candidate / component
                if self._is_link_or_junction(candidate):
                    self._reject(request, "native-artifact-path-is-indirect")
            resolved = candidate.resolve(strict=True)
            resolved.relative_to(root)
            if not resolved.is_file():
                self._reject(request, "native-artifact-unavailable")
            digest, size = sha256_file(resolved)
        except CatalogSelectionError:
            raise
        except (OSError, ValueError):
            self._reject(request, "native-artifact-unavailable")
        if digest != variant.artifact.sha256:
            self._reject(request, "native-artifact-integrity-failed")
        return size
