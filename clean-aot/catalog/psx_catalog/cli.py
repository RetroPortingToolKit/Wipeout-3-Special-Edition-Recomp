"""Command-line interface for the clean-AOT catalog."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any, Sequence

from .canonical import CanonicalizationError, canonical_json_text, load_json_file, sha256_file
from .model import Catalog, ValidationError
from .selection import CatalogSelectionError, CatalogSelector, SelectionRequest


ERROR_SCHEMA = "psxrecomp.clean-aot.catalog-error.v1"


class CliUsageError(ValueError):
    """Raised instead of printing argparse diagnostics outside the JSON contract."""


class StrictArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise CliUsageError(message)


def _load_catalog(path: str) -> Catalog:
    return Catalog.from_dict(load_json_file(path))


def _load_request(path: str) -> SelectionRequest:
    return SelectionRequest.from_dict(load_json_file(path))


def _error_json(error: Exception) -> str:
    return canonical_json_text(
        {
            "schema": ERROR_SCHEMA,
            "status": "invalid",
            "error": str(error),
        }
    )


def _build_parser() -> argparse.ArgumentParser:
    parser = StrictArgumentParser(
        prog="psx-catalog",
        description="Validate and resolve an offline precompiled executable-region catalog.",
    )
    subparsers = parser.add_subparsers(
        dest="command",
        required=True,
        parser_class=StrictArgumentParser,
    )

    validate = subparsers.add_parser("validate", help="validate a catalog manifest")
    validate.add_argument("manifest", type=Path)

    normalize = subparsers.add_parser("normalize", help="print canonical manifest JSON")
    normalize.add_argument("manifest", type=Path)

    select = subparsers.add_parser(
        "select",
        help="measure executable bytes and select a verified precompiled variant",
    )
    select.add_argument("manifest", type=Path)
    select.add_argument("request", type=Path)
    select.add_argument("executable", type=Path)
    select.add_argument("artifact_root", type=Path)

    digest = subparsers.add_parser("hash", help="hash a precompiled artifact or source file")
    digest.add_argument("file", type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    try:
        args = _build_parser().parse_args(argv)
        if args.command == "validate":
            catalog = _load_catalog(str(args.manifest))
            sys.stdout.write(
                canonical_json_text(
                    {
                        "schema": "psxrecomp.clean-aot.catalog-validation.v1",
                        "status": "valid",
                        "catalog_sha256": catalog.sha256,
                        "variant_count": len(catalog.variants),
                    }
                )
            )
            return 0
        if args.command == "normalize":
            sys.stdout.write(_load_catalog(str(args.manifest)).canonical_json())
            return 0
        if args.command == "hash":
            digest, size = sha256_file(args.file)
            sys.stdout.write(
                canonical_json_text(
                    {
                        "schema": "psxrecomp.clean-aot.file-hash.v1",
                        "size": size,
                        "sha256": digest,
                    }
                )
            )
            return 0
        if args.command == "select":
            catalog = _load_catalog(str(args.manifest))
            request = _load_request(str(args.request))
            result = CatalogSelector(catalog, args.artifact_root).select_file(
                request,
                args.executable,
            )
            sys.stdout.write(result.canonical_json())
            return 0
    except CatalogSelectionError as exc:
        sys.stdout.write(exc.receipt.canonical_json())
        return 3
    except (CanonicalizationError, CliUsageError, ValidationError, OSError, UnicodeError) as exc:
        sys.stdout.write(_error_json(exc))
        return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
