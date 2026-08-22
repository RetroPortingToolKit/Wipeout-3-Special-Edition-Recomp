"""Two-stage setup distribution and independent installed-runtime audit."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import tempfile
from pathlib import Path
from typing import Any, Sequence

if __package__ in {None, ""}:
    from release_lane import ReleaseError, validate_release  # type: ignore[import-not-found]
else:
    from .release_lane import ReleaseError, validate_release

INSTALLER_SCHEMA = "wipeout3se.clean-aot.two-stage-installer.v1"
RUNTIME_AUDIT_SCHEMA = "wipeout3se.clean-aot.installed-runtime-audit.v1"
RUNTIME_FORBIDDEN = ("compiler", "patch", "xdelta", "oracle", "legacy", "setup", "python")
RUNTIME_ROLES = {"application", "sdl-runtime", "generated-clean-artifact",
                 "configuration-defaults", "portable-launcher"}
GAME_SHARD_SCHEMA = "wo3se.generated-game-shards.v1"


def _canonical(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":")) + "\n").encode("utf-8")


def _hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _relative(value: str) -> Path:
    path = Path(value)
    if not value or path.is_absolute() or path.drive or any(p in {"", ".", ".."} for p in path.parts):
        raise ReleaseError(f"unsafe installer-relative path: {value!r}")
    return path


def _game_shard_acceptance(path: Path) -> dict[str, Any]:
    """Reduce the packaged authority to the exact entries the compiler must emit."""
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ReleaseError("missing or malformed packaged game-shard receipt") from error
    images = document.get("images")
    if (document.get("schema") != GAME_SHARD_SCHEMA or
            document.get("status") != "partial-fail-closed" or
            document.get("policy", {}).get("unknown_pc_action") != "reject" or
            document.get("policy", {}).get("unknown_identity_action") != "reject" or
            document.get("policy", {}).get("authenticated_epoch_variants") is not True or
            document.get("unresolved_cfi_site_count") != 0 or
            not isinstance(images, list) or not images):
        raise ReleaseError("packaged game-shard receipt is not fail-closed")
    entries = []
    names = set()
    for image in images:
        name = image.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise ReleaseError("packaged game-shard image names are invalid")
        names.add(name)
        entry = {key: image.get(key) for key in (
            "name", "identity_sha256", "manifest", "manifest_sha256",
            "compiled_instruction_count", "compiled_region_count")}
        if (any(not isinstance(entry[key], str) or not entry[key] for key in
                ("identity_sha256", "manifest", "manifest_sha256")) or
                any(not isinstance(entry[key], int) or entry[key] <= 0 for key in
                    ("compiled_instruction_count", "compiled_region_count"))):
            raise ReleaseError(f"packaged game-shard entry is incomplete: {name}")
        if (image.get("excluded_instruction_count") != 0 or
                image.get("authorized_instruction_count") != image.get("compiled_instruction_count")):
            raise ReleaseError(f"packaged game-shard entry is not complete: {name}")
        entries.append(entry)
    entries.sort(key=lambda item: item["name"])
    instruction_count = sum(entry["compiled_instruction_count"] for entry in entries)
    region_count = sum(entry["compiled_region_count"] for entry in entries)
    if (document.get("compiled_instruction_count") != instruction_count or
            document.get("compiled_region_count") != region_count):
        raise ReleaseError("packaged game-shard aggregate counts disagree with entries")
    epoch = document.get("epoch_dispatch", {})
    variants = sorted(epoch.get("variants", []), key=lambda item: item.get("image", ""))
    expected = sorted(({"image": item["name"], "identity_sha256": item["identity_sha256"]}
                       for item in entries), key=lambda item: item["image"])
    if (epoch.get("schema") != "wo3se.overlapping-executable-epoch-dispatch.v1" or
            epoch.get("selection_key") != "authenticated-active-image-sha256" or
            epoch.get("unknown_epoch_action") != "reject" or not epoch.get("sites") or
            variants != expected):
        raise ReleaseError("packaged game-shard epoch discriminator is invalid")
    return {"receipt_sha256": _hash(path), "authority_sha256": document.get("authority_sha256"),
            "compiled_instruction_count": instruction_count,
            "compiled_region_count": region_count, "unresolved_cfi_site_count": 0,
            "excluded_instruction_count": 0, "epoch_dispatch": epoch, "entries": entries}


def audit_installed_runtime(runtime: Path) -> dict[str, Any]:
    """Remeasure a runtime without trusting the outer installer manifest."""
    validation = validate_release(runtime)
    manifest_path = runtime / "release-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    scanned = []
    for entry in manifest["files"]:
        relative = _relative(entry["path"])
        folded = relative.as_posix().casefold()
        if entry.get("role") not in RUNTIME_ROLES:
            raise ReleaseError(f"installed runtime has forbidden role: {entry.get('role')}")
        if any(token in folded for token in RUNTIME_FORBIDDEN):
            raise ReleaseError(f"installed runtime leaks setup tooling: {relative.as_posix()}")
        scanned.append({"path": relative.as_posix(), "sha256": _hash(runtime / relative)})
    return {"schema": RUNTIME_AUDIT_SCHEMA, "status": "pass",
            "runtime_manifest_sha256": validation["manifest_sha256"],
            "files_scanned": len(scanned),
            "inventory_sha256": hashlib.sha256(_canonical(scanned)).hexdigest(),
            "forbidden_classes": list(RUNTIME_FORBIDDEN), "game_ready": False}


def create_installer(*, setup_exe: Path, runtime_payload: Path,
                     setup_payload: Sequence[tuple[str, Path]], output: Path) -> dict[str, Any]:
    """Publish setup and runtime payloads while retaining their trust boundary."""
    setup_exe = setup_exe.resolve(strict=True)
    runtime_payload = runtime_payload.resolve(strict=True)
    output = output.resolve(strict=False)
    if output.exists():
        raise ReleaseError(f"installer output already exists: {output}")
    runtime_audit = audit_installed_runtime(runtime_payload)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}-", dir=output.parent))
    try:
        shutil.copy2(setup_exe, temporary / "Wipeout3SE-Setup.exe")
        shutil.copytree(runtime_payload, temporary / "runtime-payload")
        payload_root = temporary / "setup-payload"
        entries = []
        for relative, source in setup_payload:
            target = payload_root / _relative(relative)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source.resolve(strict=True), target)
        contract = {"schema": INSTALLER_SCHEMA, "runtime_payload": "runtime-payload",
                    "installed_directory": "Wipeout3SE", "selectors": ["bios", "cue"],
                    "runtime_excludes_setup_payload": True, "game_ready": False,
                    "patch": "patch/pal_track01.xdelta3", "xdelta3": "bin/xdelta3.exe",
                    "compiler": "bin/clean-aot-compiler.exe",
                    "regions": "policy/product-regions.json",
                    "game_shards": "policy/generated-game-shards.json",
                    "game_shard_manifest_root": "policy/game-shards",
                    "artifacts": "policy/artifacts.json", "artifact_root": "native",
                    "profile": "policy/product-profile.json",
                    "media_manifest": "policy/media-manifest.json"}
        payload_root.mkdir(exist_ok=True)
        (payload_root / "setup-contract.json").write_bytes(_canonical(contract))
        game_shards = _game_shard_acceptance(payload_root / contract["game_shards"])
        for path in sorted(p for p in temporary.rglob("*") if p.is_file()):
            relative = path.relative_to(temporary).as_posix()
            entries.append({"path": relative, "size": path.stat().st_size, "sha256": _hash(path)})
        manifest = {"schema": INSTALLER_SCHEMA, "status": "packaged-not-game-ready",
                    "runtime_audit": runtime_audit, "game_shards": game_shards,
                    "files": entries}
        (temporary / "installer-manifest.json").write_bytes(_canonical(manifest))
        validate_installer(temporary)
        output.parent.mkdir(parents=True, exist_ok=True)
        os.replace(temporary, output)
        return manifest
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def validate_installer(package: Path) -> dict[str, Any]:
    package = package.resolve(strict=True)
    try:
        manifest = json.loads((package / "installer-manifest.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ReleaseError("missing or malformed installer manifest") from error
    if manifest.get("schema") != INSTALLER_SCHEMA or manifest.get("status") != "packaged-not-game-ready":
        raise ReleaseError("installer manifest schema/status mismatch")
    listed = {"installer-manifest.json"}
    for entry in manifest.get("files", []):
        relative = _relative(entry.get("path", ""))
        path = package / relative
        if relative.as_posix() in listed or not path.is_file() or path.is_symlink():
            raise ReleaseError("installer inventory mismatch")
        listed.add(relative.as_posix())
        if path.stat().st_size != entry.get("size") or _hash(path) != entry.get("sha256"):
            raise ReleaseError(f"installer payload measurement mismatch: {relative}")
    actual = {p.relative_to(package).as_posix() for p in package.rglob("*") if p.is_file()}
    if actual != listed:
        raise ReleaseError("installer contains unmanifested or missing files")
    audit = audit_installed_runtime(package / "runtime-payload")
    if audit != manifest.get("runtime_audit"):
        raise ReleaseError("nested runtime audit changed")
    contract = json.loads((package / "setup-payload" / "setup-contract.json").read_text(encoding="utf-8"))
    game_shards = _game_shard_acceptance(package / "setup-payload" / _relative(contract.get("game_shards", "")))
    if game_shards != manifest.get("game_shards"):
        raise ReleaseError("packaged game-shard acceptance changed")
    return {"schema": INSTALLER_SCHEMA, "status": "validated", "files": len(listed),
            "manifest_sha256": _hash(package / "installer-manifest.json"),
            "runtime_audit": audit, "game_shards": game_shards}
