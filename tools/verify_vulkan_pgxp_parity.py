"""Bounded static check for the WipEout renderer/PGXP parity audit.

This is deliberately not a runtime Vulkan test.  It verifies the product
configuration and, when the psxrecomp submodule is populated, checks the
renderer source surface for the replacement hook.  Missing framework source
is reported as unknown rather than treated as parity evidence.
"""
import argparse
from pathlib import Path
import re
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    config = (root / "game.toml").read_text(encoding="utf-8")
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    errors = []

    required = {
        "renderer": r'^renderer\s*=\s*"opengl"\s*$',
        "geometry_correction": r"^geometry_correction\s*=\s*true\s*$",
        "perspective_texturing": r"^perspective_texturing\s*=\s*true\s*$",
        "pgxp_depth": r"^pgxp_depth\s*=\s*true\s*$",
        "hd_textures": r"^hd_textures\s*=\s*true\s*$",
    }
    for name, pattern in required.items():
        if not re.search(pattern, config, re.MULTILINE):
            errors.append(f"missing expected configured {name}")
    if not re.search(r"psxrecomp_add_game_runtime\(psx-runtime\s+PGXP", cmake):
        errors.append("CMakeLists.txt does not select the PGXP runtime")

    framework = root / "psxrecomp"
    source_files = [framework / "runtime/src/gpu_gl_renderer.c",
                    framework / "runtime/src/gpu_vk_renderer.c",
                    framework / "runtime/src/gpu_sw_renderer.c"]
    populated = all(path.is_file() for path in source_files)
    print("PASS product configuration: OpenGL + PGXP geometry/perspective/depth + HD replacements") if not errors else None
    if errors:
        print("FAIL renderer/PGXP configuration")
        for error in errors:
            print(f"- {error}")
        return 1

    if not populated:
        print("UNKNOWN framework source: psxrecomp submodule is not populated")
        print("NOT PROVEN Vulkan parity or fallback behavior")
        return 0

    gl = (framework / "runtime/src/gpu_gl_renderer.c").read_text(encoding="utf-8")
    vk = (framework / "runtime/src/gpu_vk_renderer.c").read_text(encoding="utf-8")
    if "set_replacement" not in gl:
        print("FAIL OpenGL replacement hook not found")
        return 1
    if "set_replacement" in vk:
        print("CHECK Vulkan replacement hook present; runtime parity still unproven")
    else:
        print("CONFIRMED Vulkan replacement hook absent in checked source")
    print("NOT PROVEN Vulkan depth/PGXP runtime parity or silent-fallback behavior")
    return 0


if __name__ == "__main__":
    sys.exit(main())
