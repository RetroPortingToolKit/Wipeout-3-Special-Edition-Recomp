"""Parse and verify the framework revision snapshot against Git gitlinks."""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

PIN_PATHS = (
    ("psxrecomp", "psxrecomp"),
    ("recomp-ui", "recomp-ui"),
    ("recomp-net", "lib/recomp-net"),
    ("retcomm-rbengine", "lib/retcomm-rbengine"),
)
_PIN_LINE = re.compile(
    r"(?P<name>[a-z0-9-]+)=(?P<short>[0-9a-f]{7,40}) "
    r"\((?P<full>[0-9a-f]{40})\)"
)


@dataclass(frozen=True)
class FrameworkPin:
    """One parsed framework pin."""

    name: str
    short: str
    full: str


def parse_framework_pins(text: str) -> tuple[list[FrameworkPin], list[str]]:
    """Parse the canonical four-line snapshot, returning pins and failures."""

    expected_names = [name for name, _ in PIN_PATHS]
    lines = text.splitlines()
    errors: list[str] = []
    pins: list[FrameworkPin] = []
    if len(lines) != len(expected_names):
        errors.append(
            f"framework_pins.txt must contain exactly {len(expected_names)} lines"
        )

    for index, expected_name in enumerate(expected_names):
        if index >= len(lines):
            errors.append(f"framework_pins.txt is missing {expected_name}")
            continue
        match = _PIN_LINE.fullmatch(lines[index])
        if match is None:
            errors.append(
                f"framework_pins.txt line {index + 1} has invalid canonical format"
            )
            continue
        pin = FrameworkPin(**match.groupdict())
        pins.append(pin)
        if pin.name != expected_name:
            errors.append(
                f"framework_pins.txt line {index + 1} must be {expected_name}, "
                f"not {pin.name}"
            )
        if not pin.full.startswith(pin.short):
            errors.append(
                f"framework_pins.txt short SHA for {pin.name} is not a prefix "
                "of its full SHA"
            )
    return pins, errors


def _git(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def _gitlink(root: Path, treeish: str, path: str) -> str:
    line = _git(root, "ls-tree", treeish, "--", path)
    fields = line.split(maxsplit=3)
    if len(fields) != 4 or fields[0] != "160000" or fields[1] != "commit":
        raise ValueError(f"{path} is not a gitlink in {treeish}")
    return fields[2]


def authoritative_framework_pins(root: Path) -> dict[str, str]:
    """Resolve pins from the root index and the indexed psxrecomp tree."""

    root = root.resolve()
    psxrecomp = _git(root, "rev-parse", ":psxrecomp")
    recomp_ui = _git(root, "rev-parse", ":recomp-ui")
    framework_root = root / "psxrecomp"
    return {
        "psxrecomp": psxrecomp,
        "recomp-ui": recomp_ui,
        "recomp-net": _gitlink(framework_root, psxrecomp, "lib/recomp-net"),
        "retcomm-rbengine": _gitlink(
            framework_root, psxrecomp, "lib/retcomm-rbengine"
        ),
    }


def validate_authoritative_framework_pins(text: str, root: Path) -> list[str]:
    """Require a canonical snapshot to equal the authoritative gitlinks."""

    pins, errors = parse_framework_pins(text)
    if errors:
        return errors
    try:
        expected = authoritative_framework_pins(root)
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        return [f"cannot resolve authoritative framework gitlinks: {exc}"]

    for pin in pins:
        if pin.full != expected[pin.name]:
            errors.append(
                f"framework pin mismatch for {pin.name}: "
                f"snapshot={pin.full} gitlink={expected[pin.name]}"
            )
    return errors
