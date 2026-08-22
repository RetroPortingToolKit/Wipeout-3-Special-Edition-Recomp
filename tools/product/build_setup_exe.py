#!/usr/bin/env python3
"""Build the dependency-closed native Windows setup frontend with PyInstaller."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve(strict=False)
    output.parent.mkdir(parents=True, exist_ok=True)
    work = output.parent / ".setup-build"
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile",
               "--windowed", "--name", "Wipeout3SE-Setup", "--distpath", str(output.parent),
               "--workpath", str(work), "--specpath", str(work), "--paths", str(root),
               str(root / "tools" / "product" / "setup_frontend.py")]
    subprocess.run(command, cwd=root, check=True, stdin=subprocess.DEVNULL)
    built = output.parent / "Wipeout3SE-Setup.exe"
    if built != output:
        built.replace(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
