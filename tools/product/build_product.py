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
    from release_lane import (  # type: ignore[import-not-found]
        ReleaseError, create_release, run_soak, validate_release)
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
    from .release_lane import ReleaseError, create_release, run_soak, validate_release


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

    package = subparsers.add_parser(
        "package", help="create an isolated relocatable modern-PC package")
    package.add_argument("--app", type=Path, required=True)
    package.add_argument("--sdl", type=Path, required=True)
    package.add_argument("--artifact", action="append", default=[], metavar="RELATIVE=PATH")
    package.add_argument("--output", type=Path, required=True)
    package.add_argument("--root", type=Path, required=True)
    package.add_argument("--link-map", type=Path, required=True)
    package.add_argument("--source", type=Path, action="append", required=True)
    package.add_argument("--allowlist", type=Path)
    package.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)

    validate = subparsers.add_parser(
        "validate-release", help="remeasure a package after relocation")
    validate.add_argument("--package", type=Path, required=True)

    soak = subparsers.add_parser(
        "soak", help="run a deterministic bounded performance/soak command")
    soak.add_argument("--iterations", type=int, default=100)
    soak.add_argument("--timeout-seconds", type=float, default=30.0)
    soak.add_argument("--max-p95-ms", type=float, required=True)
    soak.add_argument("--cwd", type=Path)
    soak.add_argument("command_line", nargs=argparse.REMAINDER)
    return parser


def _artifacts(values: list[str]) -> list[tuple[str, Path]]:
    result: list[tuple[str, Path]] = []
    for value in values:
        relative, separator, source = value.partition("=")
        if not separator or not relative or not source:
            raise ReleaseError("--artifact must be RELATIVE=PATH")
        result.append((relative, Path(source)))
    return result


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
        elif args.command == "package":
            document = create_release(
                app=args.app, sdl=args.sdl, artifacts=_artifacts(args.artifact),
                output=args.output, root=args.root, link_map=args.link_map,
                sources=args.source, allowlist=args.allowlist,
                leakage_profile=args.profile)
        elif args.command == "validate-release":
            document = validate_release(args.package)
        elif args.command == "soak":
            command_line = args.command_line
            if command_line and command_line[0] == "--":
                command_line = command_line[1:]
            if not command_line:
                raise ReleaseError("soak requires a command after --")
            document = run_soak(
                command_line, iterations=args.iterations,
                timeout_seconds=args.timeout_seconds,
                max_p95_ms=args.max_p95_ms, cwd=args.cwd)
        else:  # pragma: no cover - argparse enforces the command set
            raise ProductError("unknown product command")
        _print(document)
        return 0
    except UnknownVariantError as error:
        sys.stderr.write(f"product setup rejected unknown variant: {error}\n")
        return 3
    except (ProductError, ReleaseError, OSError, ValueError) as error:
        sys.stderr.write(f"product setup failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
