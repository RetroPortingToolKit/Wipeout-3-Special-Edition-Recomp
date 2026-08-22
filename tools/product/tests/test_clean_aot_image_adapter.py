#!/usr/bin/env python3
"""Synthetic clean-aot-image-v1 adapter integration test."""

from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_clean_aot_image_adapter.py COMPILER")
    compiler = Path(sys.argv[1]).resolve(strict=True)
    image_bytes = b"\0" * 16
    image_sha = hashlib.sha256(image_bytes).hexdigest()
    variant_sha = hashlib.sha256(b"synthetic-variant").hexdigest()
    with tempfile.TemporaryDirectory(prefix="clean-aot-image-test-") as directory:
        root = Path(directory)
        image = root / "payload.bin"
        manifest = root / "payload.manifest"
        output = root / "generated"
        image.write_bytes(image_bytes)
        manifest.write_text(
            "\n".join(
                (
                    "schema=clean-aot-compiler-manifest-v1",
                    f"image_sha256={image_sha}",
                    "catalog_sha256=" + "0" * 64,
                    f"variant_sha256={variant_sha}",
                    "image_kind=raw-payload",
                    "load_start=0x00001000",
                    "load_end_exclusive=0x00001010",
                    "entry=0x00001000",
                    "code=0x00001000-0x00001010",
                    "maximum_instructions_per_shard=64",
                )
            )
            + "\n",
            encoding="utf-8",
        )
        completed = subprocess.run(
            (
                str(compiler), "--image", str(image), "--image-kind", "raw-payload",
                "--manifest", str(manifest), "--output", str(output),
            ),
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        region = (output / "clean_aot_region_00001000.cpp").read_text(encoding="utf-8")
        table = (output / "entry_table.cpp").read_text(encoding="utf-8")
        assert "clean_aot_region_00001000" in region
        assert variant_sha in table and image_sha in table
        assert "0x1000U" in table and "GeneratedExecutableBinding" in table

        bad_output = root / "bad-generated"
        image.write_bytes(image_bytes[:-4])
        rejected = subprocess.run(
            (str(compiler), "--image", str(image), "--image-kind", "raw-payload",
             "--manifest", str(manifest), "--output", str(bad_output)),
            check=False,
            capture_output=True,
            text=True,
        )
        assert rejected.returncode != 0
        assert not bad_output.exists()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
