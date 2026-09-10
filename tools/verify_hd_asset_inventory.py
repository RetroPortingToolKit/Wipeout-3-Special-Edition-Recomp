#!/usr/bin/env python3
"""Verify the source-backed HD asset/key contract without shipping assets.

With --pack, inspect a user-owned external replacement directory. Without it,
verify only checked-in configuration and inventory invariants. This deliberately
does not download, copy, decode, or generate copyrighted replacement images.
"""
from __future__ import annotations
import argparse, json, re, sys
from pathlib import Path

KEY = re.compile(r"^[0-9a-f]+-[0-9a-f]+\.png$")

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument("--pack", type=Path, help="optional user-owned external pack")
    args = ap.parse_args()
    root = args.root.resolve()
    inv = json.loads((root / "docs/audits/hd-asset-inventory.json").read_text(encoding="utf-8"))
    errors = []
    game = (root / "game.toml").read_text(encoding="utf-8")
    if not re.search(r"(?m)^renderer\s*=\s*\"opengl\"\s*$", game):
        errors.append("game.toml must select the OpenGL replacement hook")
    if not re.search(r"(?m)^hd_textures\s*=\s*true\s*$", game):
        errors.append("game.toml must enable hd_textures")
    fonts = set(inv["font_keys"])
    titles = set(inv["title_keys"]["psygnosis"]) | set(inv["title_keys"]["other_title_text"])
    if len(fonts) != 20 or len(titles) != 12:
        errors.append(f"inventory cardinality changed (fonts={len(fonts)}, titles={len(titles)})")
    result = {"font_keys": len(fonts), "title_keys": len(titles), "pack_checked": False}
    if args.pack:
        pack = args.pack.resolve()
        files = {p.name[:-4] for p in pack.glob("*.png") if KEY.match(p.name)}
        result.update({"pack_checked": True, "indexed_pngs": len(files),
                       "font_present": len(fonts & files), "title_present": len(titles & files),
                       "missing_font": sorted(fonts - files), "missing_title": sorted(titles - files)})
        if not pack.is_dir(): errors.append(f"pack is not a directory: {pack}")
        if fonts - files: errors.append(f"pack is missing {len(fonts - files)} inventoried font keys")
        if titles - files: errors.append(f"pack is missing {len(titles - files)} inventoried title keys")
    if errors:
        print(json.dumps({"status": "FAIL", "errors": errors, **result}, indent=2))
        return 1
    print(json.dumps({"status": "PASS", **result}, indent=2))
    return 0

if __name__ == "__main__":
    sys.exit(main())
