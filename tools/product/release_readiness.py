"""Deterministic, fail-closed product-objective release readiness audit."""

from __future__ import annotations

import hashlib
import json
import os
import tempfile
from pathlib import Path
from typing import Any

SCHEMA = "wipeout3se.clean-aot.release-readiness.v1"
GRAPHICS_FEATURES = {
    "complete_gp0_fallback", "exact_native_fallback", "hd_fonts", "hd_textures",
    "pgxp_vertex_projection_perspective", "scaled_dither_resolve",
    "scanout_24bit_chroma", "transparency_safe_filtering", "true_color",
    "vulkan_4k_ultrawide", "widescreen_projection_culling",
}


def write_readiness_receipt(path: Path, document: dict[str, Any]) -> None:
    """Publish canonical receipt bytes without exposing a partial receipt."""
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = (json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n").encode()
    descriptor, temporary_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp",
                                                   dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _load(path: Path) -> dict[str, Any] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else None
    except (OSError, ValueError):
        return None


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit_release_readiness(package: Path, evidence_root: Path,
                            structural: dict[str, Any]) -> dict[str, Any]:
    package, evidence_root = package.resolve(), evidence_root.resolve()
    product = evidence_root / "product" / "evidence"
    catalog = evidence_root / "catalog" / "evidence"
    repository = evidence_root.parent
    gates: list[dict[str, Any]] = []

    def label(path: Path) -> str:
        resolved = path.resolve()
        try:
            return resolved.relative_to(evidence_root).as_posix()
        except ValueError:
            try:
                return "package/" + resolved.relative_to(package).as_posix()
            except ValueError:
                return "repository/" + resolved.relative_to(repository).as_posix()

    def gate(name: str, passed: bool, evidence: list[Path], detail: str) -> None:
        gates.append({"name": name, "status": "pass" if passed else "fail",
                      "detail": detail, "evidence": [
                          {"path": label(path), "sha256": _sha(path)}
                          for path in evidence if path.is_file()]})

    acceptance_path = product / "two-stage-installer-acceptance.json"
    acceptance = _load(acceptance_path) or {}
    executable = package / "bin" / "Wipeout3SE.exe"
    setup_ok = (acceptance.get("installer", {}).get("status") == "validated" and
                acceptance.get("runtime_isolation", {}).get("status") == "passed" and
                acceptance.get("runtime_artifact", {}).get("sha256") == _sha(executable))
    gate("lawful-two-stage-setup", setup_ok, [acceptance_path],
         "setup is separate and runtime artifact identity is bound" if setup_ok else
         "current package is not bound to a validated lawful two-stage setup receipt")

    game_receipts = []
    for path in sorted(catalog.glob("*.json")):
        value = _load(path) or {}
        if (int(value.get("guest_instructions_executed", 0) or 0) > 0 or
                int(value.get("game_instructions_executed", 0) or 0) > 0):
            game_receipts.append(path)
    gate("direct-bios-to-game-execution", bool(game_receipts), game_receipts,
         "authenticated direct launch executes game instructions" if game_receipts else
         "no authenticated receipt records any game instruction")

    shards_path = catalog / "generated-game-shards.json"
    shards = _load(shards_path) or {}
    images = shards.get("images", [])
    shard_ok = (shards.get("compiled_instruction_count") == 135829 and
                shards.get("unresolved_cfi_site_count") == 0 and len(images) == 2 and
                all(item.get("authorized_instruction_count") == item.get("compiled_instruction_count") and
                    item.get("excluded_instruction_count") == 0 for item in images))
    gate("complete-shard-coverage", shard_ok, [shards_path],
         "135829/135829 instructions, zero CFI/exclusions" if shard_ok else
         "complete authenticated shard closure is absent or inconsistent")

    content_path = catalog / "content-static-coverage.json"
    ledger_path = product / "content-coverage-ledger.json"
    ledger = _load(ledger_path) or {}
    media_manifest_path = repository / "assets" / "manifests" / "wipeout3se-media.json"
    content_policy_path = evidence_root / "product" / "content-coverage-policy.json"
    inputs = ledger.get("inputs", {})
    ledger_current = (
        ledger.get("schema") == "wipeout3se.clean-aot.content-coverage-ledger.v1" and
        inputs.get("content_sha256") == _sha(content_path) and
        inputs.get("game_shards_sha256") == _sha(shards_path) and
        inputs.get("media_manifest_sha256") == _sha(media_manifest_path) and
        inputs.get("policy_sha256") == _sha(content_policy_path))
    media_coverage = ledger.get("media_availability", {})
    media_available = (ledger_current and media_coverage.get("status") == "pass" and
                       media_coverage.get("redistributed") is False and
                       media_coverage.get("disc_tracks") == 14 and
                       media_coverage.get("data_payloads") == 5179 and
                       media_coverage.get("cdda_tracks") == 13)
    gate("external-media-payload-availability", media_available, [ledger_path, content_path,
         media_manifest_path], "all 5179 data payloads and 14 tracks remain externally available"
         if media_available else "external-media inventory binding is stale or incomplete")
    static_coverage = ledger.get("static_executable_coverage", {})
    static_complete = (ledger_current and static_coverage.get("status") == "pass" and
                       static_coverage.get("authenticated_images") ==
                       ["main-exe", "nfe-entry5-route-variant"] and
                       static_coverage.get("compiled_instructions") == 135829 and
                       static_coverage.get("unresolved_cfi_sites") == 0)
    gate("static-executable-overlay-coverage", static_complete, [ledger_path, shards_path],
         "every authenticated executable/overlay is generated with zero unresolved CFI"
         if static_complete else "authenticated executable/overlay generation is incomplete")
    behavioral = ledger.get("behavioral_execution", {})
    behavioral_complete = (ledger_current and behavioral.get("status") == "pass" and
                           behavioral.get("required", 0) > 0 and
                           behavioral.get("executed") == behavioral.get("required") and
                           behavioral.get("unexecuted") == 0)
    gate("complete-behavioral-route-coverage", behavioral_complete, [ledger_path],
         "every mode/track/team/ship/menu/FMV/audio route has clean-product execution evidence"
         if behavioral_complete else
         f"{behavioral.get('unexecuted', 'unknown')} required behavioral routes remain unexecuted")

    verification_path = catalog / "verification.json"
    verification = _load(verification_path) or {}
    no_fallback = (verification.get("implementation", {}).get("interpreter_fallback") is False and
                   verification.get("implementation", {}).get("runtime_compilation") is False and
                   acceptance.get("runtime_isolation", {}).get("status") == "passed")
    gate("no-fallback-compiler-media-leakage", no_fallback,
         [verification_path, acceptance_path], "runtime isolation and no-fallback policy pass" if no_fallback else
         "fallback/compiler/media isolation proof is incomplete")

    soak_paths = [product / f"installed-soak-{profile}.json" for profile in
                  ("4k-headless", "4k-hidden", "5120x2160-headless", "5120x2160-hidden")]
    soaks = [_load(path) or {} for path in soak_paths]
    timing_ok = all(value.get("metrics", {}).get("rate_hz") == 100 and
                    value.get("metrics", {}).get("audio_sample_frames") ==
                    441 * value.get("metrics", {}).get("updates", -1) and
                    value.get("metrics", {}).get("audio_underruns") == 0 and
                    value.get("metrics", {}).get("gpu_submission_failures") == 0 and
                    value.get("metrics", {}).get("pacing_late_updates") == 0 for value in soaks)
    gate("exact-100hz-simulation-audio-presentation", timing_ok, soak_paths,
         "all four workloads preserve exact 100 Hz and 441 audio frames/update" if timing_ok else
         "100 Hz/audio/presentation workload evidence is incomplete")

    shell_path = catalog / "bios-shell-cd-execution-02725528.json"
    shell = _load(shell_path) or {}
    media_ok = int(shell.get("cd", {}).get("raw_sectors_read", 0) or 0) > 0
    gate("authenticated-media-execution", media_ok, [shell_path],
         "authenticated runtime reads verified game media" if media_ok else
         "authenticated BIOS-to-game path still records zero media sectors")

    graphics_path = product / "graphics-matrix.json"
    graphics = _load(graphics_path) or {}
    graphics_profiles = graphics.get("profiles", {})
    expected_profiles = {"4k-headless", "4k-hidden", "5120x2160-headless",
                         "5120x2160-hidden"}
    graphics_ok = (
        graphics.get("schema") == "wipeout3se.clean-aot.graphics-feature-matrix.v1" and
        graphics.get("status") == "pass" and graphics.get("matrix_complete") is True and
        graphics.get("package_manifest_sha256") == structural.get("manifest_sha256") and
        graphics.get("runtime_executable_sha256") == _sha(executable) and
        graphics.get("settings_sha256") == _sha(package / "policy" / "default-config.json") and
        graphics.get("physical_present_timing") in {"unavailable", "verified"} and
        set(graphics_profiles) == expected_profiles and
        all(set(value.get("features", {})) == GRAPHICS_FEATURES and
            all(enabled is True for enabled in value["features"].values()) and
            value.get("physical_present_timing") in {"unavailable", "verified"}
            for value in graphics_profiles.values()))
    gate("graphics-feature-matrix", graphics_ok, [graphics_path],
         "4K/ultrawide/PGXP/true-color/filter/HD matrix passes" if graphics_ok else
         "no complete product-bound graphics matrix receipt")

    persistence_path = product / "persistence-acceptance.json"
    persistence = _load(persistence_path) or {}
    persistence_cards = persistence.get("cards", {})
    persistence_ok = (
        persistence.get("schema") == "wipeout3se.clean-aot.persistence-acceptance.v1" and
        persistence.get("status") == "passed" and
        persistence.get("package_manifest_sha256") == structural.get("manifest_sha256") and
        persistence.get("runtime_executable_sha256") == _sha(executable) and
        persistence.get("persistence_model") == "dual-128k-memory-cards-only" and
        persistence.get("emulator_save_states") is False and
        persistence.get("runtime_snapshots") is False and
        persistence.get("save_state_formats") == [] and
        persistence.get("save_state_leaks") == [] and
        len(persistence.get("runtime_persistence_symbols", [])) == 3 and
        persistence_cards.get("slots") == 2 and
        persistence_cards.get("bytes_each") == 128 * 1024 and
        persistence_cards.get("transactional_replace") is True and
        persistence_cards.get("backup_rotation_depth") == 2 and
        persistence_cards.get("corruption_detection") is True and
        persistence_cards.get("automatic_recovery") is True and
        persistence.get("install", {}).get("unicode_path") is True and
        persistence.get("upgrade", {}).get("cards_preserved") is True and
        persistence.get("rollback", {}).get("cards_preserved") is True and
        persistence.get("scripted_input", {}).get("rate_hz") == 100 and
        persistence.get("scripted_input", {}).get("scenario_count") == 64)
    gate("memory-card-persistence-recovery", persistence_ok, [persistence_path],
         "dual cards pass transactional, corruption, recovery, Unicode, upgrade, and rollback acceptance"
         if persistence_ok else "no current product-bound dual-card persistence acceptance")

    config = _load(package / "policy" / "default-config.json") or {}
    boundary_ok = (config.get("schema") == "wipeout3se.runtime-settings.v2" and
                   config.get("saves", {}).get("atomic") is True and
                   config.get("input", {}).get("profile") == "modern-gamepad" and
                   config.get("audio", {}).get("sample_rate_hz") == 44100)
    gate("input-saves-audio-settings", boundary_ok, [package / "policy" / "default-config.json"],
         "validated persistent modern settings contract" if boundary_ok else
         "installed settings boundary is missing or invalid")

    gate("relocatable-package", structural.get("status") == "validated", [],
         "manifest and portable launcher remeasure after relocation")
    current_manifest = structural.get("manifest_sha256")
    soak_bound = all(value.get("package_manifest_sha256") == current_manifest for value in soaks)
    gate("current-product-performance-soak", timing_ok and soak_bound, soak_paths,
         "deterministic soak receipts are bound to this package" if timing_ok and soak_bound else
         "soak receipts are stale or not cryptographically bound to this package")

    evidence_items = [item for item in gates for item in item["evidence"]]
    evidence_bindings: dict[str, set[str]] = {}
    for item in evidence_items:
        evidence_bindings.setdefault(item["path"], set()).add(item["sha256"])
    receipt_integrity = (
        isinstance(current_manifest, str) and len(current_manifest) == 64 and
        bool(evidence_bindings) and
        all(len(hashes) == 1 for hashes in evidence_bindings.values()) and
        all(len(item["sha256"]) == 64 and
            all(character in "0123456789abcdef" for character in item["sha256"])
            for item in evidence_items))
    gate("deterministic-receipt-integrity", receipt_integrity, [],
         "canonical gate order and unique SHA-256 evidence bindings are complete"
         if receipt_integrity else "receipt evidence bindings are missing, duplicated, or malformed")

    ready = all(item["status"] == "pass" for item in gates)
    return {"schema": SCHEMA, "status": "ready" if ready else "not-ready",
            "game_ready": ready, "package_manifest_sha256": current_manifest,
            "passed": sum(item["status"] == "pass" for item in gates),
            "failed": sum(item["status"] == "fail" for item in gates), "gates": gates}
