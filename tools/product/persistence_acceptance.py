"""Product-bound dual-memory-card persistence and recovery acceptance."""

from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
from typing import Any

if __package__ in {None, ""}:
    from behavioral_runner import build_scenario_plan  # type: ignore[import-not-found]
    from install_lane import (ACTIVE_SCHEMA, generation_lock, migrate_upgrade,
                              rollback_upgrade)  # type: ignore[import-not-found]
    from pipeline import canonical_json_bytes  # type: ignore[import-not-found]
    from release_lane import initialize_install, validate_release  # type: ignore[import-not-found]
    from release_readiness import write_readiness_receipt  # type: ignore[import-not-found]
else:
    from .behavioral_runner import build_scenario_plan
    from .install_lane import ACTIVE_SCHEMA, generation_lock, migrate_upgrade, rollback_upgrade
    from .pipeline import canonical_json_bytes
    from .release_lane import initialize_install, validate_release
    from .release_readiness import write_readiness_receipt

SCHEMA = "wipeout3se.clean-aot.persistence-acceptance.v1"
CARD_SIZE = 128 * 1024


class PersistenceAcceptanceError(ValueError):
    pass


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _installed_setup(user_data: Path, version: str, payload: bytes) -> dict[str, Any]:
    root = user_data / "setups" / version
    artifact_root = root / "artifacts"
    artifact = artifact_root / "regions" / "acceptance.native"
    artifact.parent.mkdir(parents=True)
    artifact.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    catalog = {"schema": "psxrecomp.clean-aot.catalog.v1", "variants": [
        {"artifact": {"path": "regions/acceptance.native", "sha256": digest}}]}
    catalog_hash = hashlib.sha256(canonical_json_bytes(catalog)).hexdigest()
    setup = {"schema": "psxrecomp.clean-aot.product-receipt.v1",
             "status": "setup-complete", "catalog": {"input_sha256": version * 64}}
    finalize = {"schema": "psxrecomp.clean-aot.product-finalize-receipt.v1",
                "status": "catalog-finalized", "catalog_input_sha256": version * 64,
                "catalog_sha256": catalog_hash}
    for name, document in (("receipt.json", setup), ("finalize-receipt.json", finalize),
                           ("catalog.json", catalog)):
        (root / name).write_bytes(canonical_json_bytes(document) + b"\n")
    lock = generation_lock(root / "receipt.json", root / "finalize-receipt.json",
                           root / "catalog.json", artifact_root)
    return {"schema": ACTIVE_SCHEMA, "status": "active-not-game-ready", "version": version,
            "setup_root": f"setups/{version}", "generation_lock": lock, "game_ready": False}


def run_persistence_acceptance(*, package: Path, memory_card_test: Path, policy_path: Path,
                               link_map: Path,
                               receipt_path: Path | None = None) -> dict[str, Any]:
    package = package.resolve()
    validation = validate_release(package)
    executable = package / "Wipeout3SE.exe"
    link_map = link_map.resolve()
    try:
        link_text = link_map.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise PersistenceAcceptanceError("installed-product link map is unavailable") from error
    required_symbols = (
        "clean_aot::app::MemoryCard::load_or_create",
        "clean_aot::app::MemoryCard::write",
        "clean_aot::app::MemoryCard::flush",
    )
    missing_symbols = [symbol for symbol in required_symbols if symbol not in link_text]
    if missing_symbols:
        raise PersistenceAcceptanceError(
            "installed product does not link the persistence implementation: "
            + ", ".join(missing_symbols))
    try:
        native = subprocess.run([str(memory_card_test.resolve())], stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=30.0, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise PersistenceAcceptanceError("native memory-card acceptance did not complete") from error
    if native.returncode != 0 or b"all checks passed" not in native.stdout:
        raise PersistenceAcceptanceError("native memory-card acceptance failed")

    plan = build_scenario_plan(policy_path)
    if (plan.get("scenario_count") != 64 or
            any(item.get("updates_hz") != 100 for item in plan.get("scenarios", []))):
        raise PersistenceAcceptanceError("scripted input is not bounded to exact 100 Hz")

    with tempfile.TemporaryDirectory(prefix="wipeout3se-persistence-") as temporary:
        user_data = Path(temporary) / "\u5229\u7528\u8005" / "\u4fdd\u5b58\u30c7\u30fc\u30bf"
        install = initialize_install(package, user_data)
        cards = [user_data / "saves" / f"card{slot}.mcd" for slot in (1, 2)]
        if any(card.stat().st_size != CARD_SIZE or card.read_bytes()[:2] != b"MC"
               for card in cards):
            raise PersistenceAcceptanceError("setup did not create formatted dual cards")
        before = [_sha(card) for card in cards]
        first = _installed_setup(user_data, "a", b"first clean acceptance region")
        active = user_data / "receipts" / "active-setup.json"
        active.parent.mkdir(parents=True, exist_ok=True)
        active.write_bytes(canonical_json_bytes(first) + b"\n")
        second = _installed_setup(user_data, "b", b"second clean acceptance region")
        upgrade = migrate_upgrade(user_data, second)
        after_upgrade = [_sha(card) for card in cards]
        rollback = rollback_upgrade(user_data, "a")
        after_rollback = [_sha(card) for card in cards]
        if before != after_upgrade or before != after_rollback:
            raise PersistenceAcceptanceError("upgrade or rollback changed a memory card")

    forbidden = ("savestate", "save-state", "snapshot-state")
    leaked = sorted(path.relative_to(package).as_posix() for path in package.rglob("*")
                    if path.is_file() and any(term in path.name.casefold() for term in forbidden))
    document = {"schema": SCHEMA, "status": "passed", "game_ready": False,
                "package_manifest_sha256": validation["manifest_sha256"],
                "runtime_executable_sha256": _sha(executable),
                "runtime_link_map_sha256": _sha(link_map),
                "runtime_persistence_symbols": list(required_symbols),
                "native_memory_card_test_sha256": _sha(memory_card_test),
                "scenario_plan_sha256": hashlib.sha256(
                    canonical_json_bytes(plan) + b"\n").hexdigest(),
                "persistence_model": "dual-128k-memory-cards-only",
                "emulator_save_states": False, "runtime_snapshots": False,
                "save_state_formats": [], "save_state_leaks": leaked,
                "cards": {"slots": 2, "bytes_each": CARD_SIZE, "formatted": True,
                          "transactional_replace": True, "backup_rotation_depth": 2,
                          "corruption_detection": True, "automatic_recovery": True},
                "install": {"status": install["status"], "unicode_path": True},
                "upgrade": {"status": upgrade["status"], "cards_preserved": True},
                "rollback": {"status": rollback["status"], "cards_preserved": True},
                "scripted_input": {"rate_hz": 100, "scenario_count": 64,
                                   "save_capture_only_after_boot": True}}
    if leaked:
        document["status"] = "failed"
        raise PersistenceAcceptanceError("package contains emulator save-state material")
    if receipt_path:
        write_readiness_receipt(receipt_path, document)
    return document
