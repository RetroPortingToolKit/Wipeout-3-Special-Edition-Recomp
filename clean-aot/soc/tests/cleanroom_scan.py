from __future__ import annotations

import pathlib
import re
import sys


def word_pattern(*letters: str) -> re.Pattern[str]:
    return re.compile(r"\b" + "".join(letters) + r"\b", re.IGNORECASE)


FORBIDDEN = (
    word_pattern("p", "s", "x", "r", "e", "c", "o", "m", "p"),
    word_pattern("d", "u", "c", "k", "s", "t", "a", "t", "i", "o", "n"),
    word_pattern("g", "o", "o", "s", "e", "s", "t", "a", "t", "i", "o", "n"),
    word_pattern("j", "i", "t"),
    word_pattern(
        "i", "n", "t", "e", "r", "p", "r", "e", "t", "e", "r"
    ),
    word_pattern("e", "m", "u", "l", "a", "t", "o", "r"),
)


def main() -> int:
    root = pathlib.Path(sys.argv[1]).resolve()
    checked = 0
    findings: list[str] = []
    for path in sorted(root.rglob("*")):
        if path.suffix.lower() not in {
            ".cpp",
            ".hpp",
            ".h",
            ".c",
            ".cmake",
        }:
            continue
        checked += 1
        text = path.read_text(encoding="utf-8")
        for pattern in FORBIDDEN:
            if pattern.search(text):
                findings.append(f"{path}: {pattern.pattern}")
    if findings:
        for finding in findings:
            print(finding)
        return 1
    print(f"cleanroom scan: {checked} source files, 0 findings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
