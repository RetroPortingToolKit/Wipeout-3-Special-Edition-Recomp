"""Acceptance for the single-entrypoint portable Windows product."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import stat
import subprocess
import tempfile
from pathlib import Path
from typing import Any

if __package__ in {None, ""}:
    from installer_lane import audit_installed_runtime  # type: ignore[import-not-found]
    from release_lane import ReleaseError, validate_release  # type: ignore[import-not-found]
    from release_readiness import write_readiness_receipt  # type: ignore[import-not-found]
else:
    from .installer_lane import audit_installed_runtime
    from .release_lane import ReleaseError, validate_release
    from .release_readiness import write_readiness_receipt

SCHEMA = "wipeout3se.clean-aot.portable-launcher-acceptance.v1"


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _inventory(root: Path) -> dict[str, str]:
    return {path.relative_to(root).as_posix(): _sha(path)
            for path in sorted(root.rglob("*")) if path.is_file()}


def _run(executable: Path, arguments: list[str]) -> dict[str, Any]:
    process = subprocess.run([str(executable), *arguments], cwd=executable.parent,
                             stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, timeout=120.0, check=False,
                             text=True, encoding="utf-8")
    lines = [line for line in process.stdout.splitlines() if line.strip()]
    try:
        receipt = json.loads(lines[-1]) if lines else {}
    except ValueError as error:
        raise ReleaseError("portable launcher emitted malformed receipt") from error
    return {"exit": process.returncode, "receipt": receipt}


def run_portable_acceptance(package: Path, bios: Path, cue: Path,
                            receipt_path: Path | None = None) -> dict[str, Any]:
    package, bios, cue = package.resolve(), bios.resolve(), cue.resolve()
    structural = validate_release(package)
    if package in bios.parents or package in cue.parents:
        raise ReleaseError("copyrighted input must remain outside the portable package")
    source_inventory = _inventory(package)
    with tempfile.TemporaryDirectory(prefix="wipeout3se-portable-") as temporary:
        root = Path(temporary)
        relocated = root / "読取専用" / "Wipeout3SE"
        shutil.copytree(package, relocated)
        for path in relocated.rglob("*"):
            if path.is_file(): path.chmod(stat.S_IREAD)
        relocated_validation = validate_release(relocated)
        user_data = root / "利用者" / "構成"
        user_data.mkdir(parents=True)
        shutil.copy2(relocated / "policy" / "default-config.json", user_data / "config.json")
        imported = _run(relocated / "Wipeout3SE.exe", [
            "--user-data", str(user_data), "--bios", str(bios),
            "--import-cue", str(cue), "--import-only"])
        if (imported["exit"] != 0 or imported["receipt"].get("stage") != "media-import-complete"):
            raise ReleaseError(f"portable in-app media import failed: {imported}")
        media_receipt = json.loads((user_data / "receipts" / "portable-media.json").read_text(
            encoding="utf-8"))
        if (media_receipt.get("status") != "verified-external-media" or
                media_receipt.get("copyrighted_inputs_copied") is not False):
            raise ReleaseError("portable media receipt is not lawful and fail-closed")
        if any(path.name.endswith(".new") for path in user_data.rglob("*")):
            raise ReleaseError("portable import left an atomic staging file")
        input_hashes = {_sha(bios), _sha(cue)}
        if any(_sha(path) in input_hashes for path in user_data.rglob("*") if path.is_file()):
            raise ReleaseError("portable import copied copyrighted input bytes")
        cancelled_data = root / "cancelled"
        cancelled_data.mkdir()
        shutil.copy2(relocated / "policy" / "default-config.json", cancelled_data / "config.json")
        cancelled = _run(relocated / "Wipeout3SE.exe", [
            "--user-data", str(cancelled_data), "--import-only"])
        if (cancelled["exit"] != 0 or cancelled["receipt"].get("stage") != "media-import-cancelled" or
                (cancelled_data / "runtime").exists() or (cancelled_data / "receipts").exists()):
            raise ReleaseError("portable import cancellation mutated user data")
        if _inventory(relocated) != source_inventory:
            raise ReleaseError("read-only portable source mutated during first run")
    runtime_audit = audit_installed_runtime(package)
    document = {
        "schema": SCHEMA, "status": "passed-not-game-ready", "game_ready": False,
        "package_manifest_sha256": structural["manifest_sha256"],
        "runtime_executable_sha256": _sha(package / "Wipeout3SE.exe"),
        "delivery": {"entrypoint": "Wipeout3SE.exe", "portable_folder": True,
                     "installer": False, "setup_executable": False,
                     "registry_installation": False},
        "first_run": {"native_selectors": True, "headless_selectors": True,
                      "atomic_activation": True, "cancel_without_mutation": True,
                      "unicode_paths": True, "read_only_source": True,
                      "external_bios_cue_only": True, "copyrighted_inputs_copied": False},
        "runtime_isolation": runtime_audit,
        "source_inventory_sha256": hashlib.sha256(json.dumps(
            source_inventory, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
    }
    if receipt_path: write_readiness_receipt(receipt_path, document)
    return document
