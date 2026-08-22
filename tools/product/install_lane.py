"""Atomic first-run activation and fail-closed launch receipts."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any, Sequence

if __package__ in {None, ""}:
    from pipeline import (DEFAULT_MEDIA_MANIFEST, DEFAULT_PROFILE, canonical_json_bytes,
                          finalize_catalog, load_json, setup_product)  # type: ignore[import-not-found]
else:
    from .pipeline import (DEFAULT_MEDIA_MANIFEST, DEFAULT_PROFILE, canonical_json_bytes,
                           finalize_catalog, load_json, setup_product)


class InstallError(RuntimeError):
    pass


ACTIVE_SCHEMA = "wipeout3se.clean-aot.active-setup.v1"
BOOT_SCHEMA = "wipeout3se.clean-aot.boot-frontier.v1"


def _hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _atomic(path: Path, document: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".new")
    if temporary.exists(): temporary.unlink()
    with temporary.open("xb") as stream:
        stream.write(canonical_json_bytes(document) + b"\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def generation_lock(setup: Path, finalize: Path, catalog: Path,
                    artifact_root: Path) -> dict[str, Any]:
    setup_doc, final_doc, catalog_doc = load_json(setup), load_json(finalize), load_json(catalog)
    if setup_doc.get("status") != "setup-complete" or final_doc.get("status") != "catalog-finalized":
        raise InstallError("setup is not finalized")
    if setup_doc.get("catalog", {}).get("input_sha256") != final_doc.get("catalog_input_sha256"):
        raise InstallError("finalized catalog is stale for this setup")
    catalog_hash = hashlib.sha256(canonical_json_bytes(catalog_doc)).hexdigest()
    if catalog_hash != final_doc.get("catalog_sha256"):
        raise InstallError("finalized catalog measurement mismatch")
    artifacts = []
    root = artifact_root.resolve(strict=True)
    for variant in catalog_doc.get("variants", []):
        artifact = variant.get("artifact")
        if not isinstance(artifact, dict): continue
        relative = Path(str(artifact.get("path", "")))
        if relative.is_absolute() or ".." in relative.parts:
            raise InstallError("unsafe generated artifact path")
        candidate = (root / relative).resolve(strict=True)
        try: candidate.relative_to(root)
        except ValueError as error: raise InstallError("generated artifact escapes root") from error
        digest = _hash(candidate)
        if digest != artifact.get("sha256"):
            raise InstallError("stale generated artifact")
        artifacts.append({"path": relative.as_posix(), "sha256": digest,
                          "size": candidate.stat().st_size})
    if not artifacts: raise InstallError("finalized catalog contains no generated artifacts")
    return {"schema": "wipeout3se.clean-aot.generation-lock.v1",
            "setup_sha256": _hash(setup), "finalize_sha256": _hash(finalize),
            "catalog_sha256": catalog_hash, "artifacts": sorted(artifacts, key=lambda x: x["path"])}


def orchestrate_first_run(*, cue: Path, bios: Path, patch: Path, xdelta3: Path,
                          compiler: Path, regions: Path, artifacts: Path,
                          artifact_root: Path, user_data: Path,
                          profile: Path = DEFAULT_PROFILE,
                          media_manifest: Path = DEFAULT_MEDIA_MANIFEST) -> dict[str, Any]:
    user_data = user_data.resolve(strict=False)
    user_data.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix="setup-", dir=user_data))
    try:
        setup_product(cue=cue, bios=bios, patch=patch, xdelta3=xdelta3,
                      compiler=compiler, output=staging / "prepared", profile_path=profile,
                      media_manifest=media_manifest, regions_path=regions)
        prepared = staging / "prepared"
        finalize_catalog(catalog_input_path=prepared / "catalog-input.json",
                         artifacts_path=artifacts, artifact_root=artifact_root,
                         output=prepared, profile_path=profile)
        external_lock = generation_lock(prepared / "receipt.json",
                                        prepared / "finalize-receipt.json",
                                        prepared / "catalog.json", artifact_root)
        for item in external_lock["artifacts"]:
            source = artifact_root / item["path"]
            target = prepared / "artifacts" / item["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        lock = generation_lock(prepared / "receipt.json",
                               prepared / "finalize-receipt.json",
                               prepared / "catalog.json", prepared / "artifacts")
        version = lock["setup_sha256"][:16]
        destination = user_data / "setups" / version
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists(): raise InstallError("this setup version is already installed")
        os.replace(prepared, destination)
        active = {"schema": ACTIVE_SCHEMA, "status": "active-not-game-ready",
                  "version": version, "setup_root": f"setups/{version}",
                  "generation_lock": lock, "game_ready": False}
        _atomic(user_data / "receipts" / "active-setup.json", active)
        return active
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    finally:
        if staging.exists(): shutil.rmtree(staging, ignore_errors=True)


def launch_and_record(package: Path, user_data: Path, *, timeout_seconds: float = 30.0,
                      command: Sequence[str] | None = None) -> dict[str, Any]:
    if __package__ in {None, ""}:
        from release_lane import validate_release  # type: ignore[import-not-found]
    else:
        from .release_lane import validate_release
    package_receipt = validate_release(package)
    active_path = user_data / "receipts" / "active-setup.json"
    active = load_json(active_path)
    if active.get("schema") != ACTIVE_SCHEMA or active.get("game_ready") is not False:
        raise InstallError("active setup receipt is invalid")
    setup_root = user_data / active["setup_root"]
    current = generation_lock(setup_root / "receipt.json", setup_root / "finalize-receipt.json",
                              setup_root / "catalog.json", setup_root / "artifacts")
    if current != active.get("generation_lock"):
        raise InstallError("active generated artifacts are stale")
    launch = list(command) if command is not None else [str(package / "bin" / "Wipeout3SE.exe"),
                                                        "--user-data", str(user_data)]
    completed = subprocess.run(launch, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=timeout_seconds, check=False)
    try: frontier = json.loads(completed.stdout.decode("utf-8").strip().splitlines()[-1])
    except (UnicodeError, ValueError, IndexError) as error:
        raise InstallError("application emitted no canonical boot frontier") from error
    if (not isinstance(frontier, dict)
            or frontier.get("schema") != "clean-aot.bootstrap-receipt.v1"
            or not isinstance(frontier.get("stage"), str)
            or type(frontier.get("bios_instructions_executed")) is not int
            or frontier["bios_instructions_executed"] < 0):
        raise InstallError("application boot frontier schema is invalid")
    receipt = {"schema": BOOT_SCHEMA, "status": "frontier-recorded-not-game-ready",
               "exit_code": completed.returncode,
               "package_manifest_sha256": package_receipt["manifest_sha256"],
               "active_setup_sha256": _hash(active_path), "frontier": frontier,
               "stdout_sha256": hashlib.sha256(completed.stdout).hexdigest(),
               "stderr_sha256": hashlib.sha256(completed.stderr).hexdigest(),
               "game_ready": False}
    _atomic(user_data / "receipts" / "boot-frontier.json", receipt)
    return receipt
