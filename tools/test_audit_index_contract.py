from __future__ import annotations

import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT_DIR = ROOT / "docs" / "audits"
INDEX = AUDIT_DIR / "README.md"
MARKDOWN_LINK = re.compile(r"\[[^]]+\]\(([^)#?]+\.md)(?:[?#][^)]*)?\)")
NUMERIC_HANDOFF_CITATION = re.compile(r"(?:docs/|\.\./)?HANDOFF\.md(?::\d|#L\d)")
LEGACY_NUMERIC_CITATION_FILES = {
    "boot-cd.md",
    "feature-state-persistence.md",
    "goosestation-parity.md",
    "goosestation-pgxp-deep.md",
    "goosestation-skip-engine.md",
}


def test_audit_index_links_every_non_semantic_audit_exactly_once() -> None:
    expected = {
        path.name
        for path in AUDIT_DIR.glob("*.md")
        if path.name != INDEX.name and not path.name.startswith("semantic-")
    }
    targets = [
        Path(target).name
        for target in MARKDOWN_LINK.findall(INDEX.read_text(encoding="utf-8"))
        if Path(target).parent == Path(".")
    ]
    counts = Counter(targets)

    assert set(counts) == expected
    assert all(count == 1 for count in counts.values())


def test_audit_index_uses_current_docs_and_new_audits_use_stable_links() -> None:
    text = INDEX.read_text(encoding="utf-8")

    assert "../HANDOFF.md" in text
    assert "../WIDESCREEN_RELEASE_PLAN.md" in text
    offenders = {
        path.name
        for path in AUDIT_DIR.glob("*.md")
        if path.name not in LEGACY_NUMERIC_CITATION_FILES
        and NUMERIC_HANDOFF_CITATION.search(path.read_text(encoding="utf-8"))
    }
    assert offenders == set()
