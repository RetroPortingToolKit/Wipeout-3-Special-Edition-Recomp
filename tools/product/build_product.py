#!/usr/bin/env python3
"""Build the external, offline clean-AOT product setup."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

if __package__ in {None, ""}:
    from pipeline import (  # type: ignore[import-not-found]
        DEFAULT_MEDIA_MANIFEST,
        DEFAULT_PROFILE,
        ProductError,
        UnknownVariantError,
        finalize_catalog,
        run_dry_run,
        setup_product,
    )
else:
    from .pipeline import (
        DEFAULT_MEDIA_MANIFEST,
        DEFAULT_PROFILE,
        ProductError,
        UnknownVariantError,
        finalize_catalog,
        run_dry_run,
        setup_product,
    )


def _common_media(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--cue", type=Path, required=True)
    parser.add_argument("--bios", type=Path, required=True)
    parser.add_argument("--patch", type=Path, required=True)
    parser.add_argument("--xdelta3", type=Path, required=True)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--media-manifest", type=Path, default=DEFAULT_MEDIA_MANIFEST)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="offline static/region clean-AOT product pipeline"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    dry = subparsers.add_parser("dry-run", help="print a side-effect-free plan receipt")
    _common_media(dry)
    dry.add_argument("--compiler", type=Path)
    dry.add_argument("--regions", type=Path)
    dry.add_argument("--output", type=Path)
    dry.add_argument("--receipt", type=Path)

    setup = subparsers.add_parser(
        "setup",
        help="verify media, extract bounded inputs, invoke the offline compiler, and publish sources",
    )
    _common_media(setup)
    setup.add_argument("--compiler", type=Path, required=True)
    setup.add_argument("--regions", type=Path, required=True)
    setup.add_argument("--output", type=Path, required=True)
    setup.add_argument(
        "--compiler-interface",
        choices=("clean-aot-v1", "clean-aot-image-v1"),
        default="clean-aot-v1",
    )
    setup.add_argument("--artifacts", type=Path)
    setup.add_argument("--artifact-root", type=Path)

    finalize = subparsers.add_parser(
        "finalize",
        help="validate external native artifacts and publish a strict catalog",
    )
    finalize.add_argument("--input", type=Path, required=True)
    finalize.add_argument("--artifacts", type=Path, required=True)
    finalize.add_argument("--artifact-root", type=Path, required=True)
    finalize.add_argument("--output", type=Path, required=True)
    finalize.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    return parser


def _print(document: Any) -> None:
    sys.stdout.write(
        json.dumps(document, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    )


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    try:
        args = parser.parse_args(argv)
        if args.command == "dry-run":
            document = run_dry_run(
                cue=args.cue,
                bios=args.bios,
                patch=args.patch,
                xdelta3=args.xdelta3,
                compiler=args.compiler,
                profile_path=args.profile,
                regions_path=args.regions,
                output=args.output,
                receipt_path=args.receipt,
            )
        elif args.command == "setup":
            document = setup_product(
                cue=args.cue,
                bios=args.bios,
                patch=args.patch,
                xdelta3=args.xdelta3,
                compiler=args.compiler,
                output=args.output,
                profile_path=args.profile,
                media_manifest=args.media_manifest,
                regions_path=args.regions,
                compiler_interface=args.compiler_interface,
                artifacts_path=args.artifacts,
                artifact_root=args.artifact_root,
            )
        elif args.command == "finalize":
            document = finalize_catalog(
                catalog_input_path=args.input,
                artifacts_path=args.artifacts,
                artifact_root=args.artifact_root,
                output=args.output,
                profile_path=args.profile,
            )
        else:  # pragma: no cover - argparse enforces the command set
            raise ProductError("unknown product command")
        _print(document)
        return 0
    except UnknownVariantError as error:
        sys.stderr.write(f"product setup rejected unknown variant: {error}\n")
        return 3
    except (ProductError, OSError, ValueError) as error:
        sys.stderr.write(f"product setup failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
