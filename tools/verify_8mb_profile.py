"""Fail-closed check for the WipEout high-detail launch profile.

The runtime intentionally defaults to retail 2 MB when no 8 MB activation is
selected. This diagnostic makes that fallback explicit at the launcher
boundary; it does not modify guest RAM or save-state behavior.
"""
import argparse
import tomllib
from pathlib import Path


RAM_PACKAGE = "psx.enhancement.8mb-ram"
REQUIRED_OVERCLOCK = 900


def validate(game_config, mod_state):
    errors = []
    if not isinstance(game_config, dict):
        return ["game.toml must be a TOML table"]
    video = game_config.get("video")
    if not isinstance(video, dict) or video.get("cpu_overclock") != REQUIRED_OVERCLOCK:
        errors.append("[video].cpu_overclock must explicitly be 900 for the 8 MB profile")

    if not isinstance(mod_state, dict):
        errors.append("mods/state.toml must be present and valid")
        return errors
    features = mod_state.get("feature")
    selected = any(
        isinstance(feature, dict)
        and feature.get("package_id") == RAM_PACKAGE
        and feature.get("enabled") is True
        for feature in (features if isinstance(features, list) else [])
    )
    if not selected:
        errors.append(
            f"mods/state.toml must explicitly enable {RAM_PACKAGE}; refusing silent 2 MB fallback"
        )
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="release or project directory")
    args = parser.parse_args()
    root = args.root
    try:
        with (root / "game.toml").open("rb") as stream:
            game_config = tomllib.load(stream)
        with (root / "mods" / "state.toml").open("rb") as stream:
            mod_state = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        print(f"FAIL 8 MB profile: {error}")
        return 1
    errors = validate(game_config, mod_state)
    if errors:
        print("FAIL 8 MB profile")
        for error in errors:
            print(f"- {error}")
        return 1
    print("PASS 8 MB profile: explicit RAM activation and 900% pairing present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
