"""Deterministic lawful-media, static-code, and behavioral coverage ledger."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Sequence

if __package__ in {None, ""}:
    from release_readiness import write_readiness_receipt  # type: ignore[import-not-found]
else:
    from .release_readiness import write_readiness_receipt

SCHEMA = "wipeout3se.clean-aot.content-coverage-ledger.v1"


class ContentCoverageError(ValueError):
    """Coverage inputs do not form the strict product contract."""


def _load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ContentCoverageError(f"missing or malformed coverage input: {path}") from error
    if not isinstance(value, dict):
        raise ContentCoverageError(f"coverage input is not an object: {path}")
    return value


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_content_coverage_ledger(*, content_path: Path, shards_path: Path,
                                  media_manifest_path: Path, policy_path: Path,
                                  behavioral_evidence: Sequence[Path] = (),
                                  receipt_path: Path | None = None) -> dict[str, Any]:
    content = _load(content_path)
    shards = _load(shards_path)
    media = _load(media_manifest_path)
    policy = _load(policy_path)
    if (content.get("schema") != "wo3se.content-static-coverage.v1" or
            shards.get("schema") != "wo3se.generated-game-shards.v1" or
            media.get("format_version") != 1 or
            policy.get("schema") != "wipeout3se.content-coverage-policy.v1" or
            media.get("game", {}).get("id") != policy.get("game_id")):
        raise ContentCoverageError("coverage input schema or game identity mismatch")

    payloads = content.get("media_payloads", [])
    exclusions = content.get("media_exclusions", [])
    audio_tracks = media.get("audio_tracks", [])
    expected_exclusions = {f"/TRACK{number:02}.WAV" for number in range(1, 14)}
    media_ok = (
        media.get("game", {}).get("cue_tracks") == 14 and len(audio_tracks) == 13 and
        [item.get("track") for item in audio_tracks] == list(range(2, 15)) and
        all(isinstance(item.get("size"), int) and item["size"] > 0 and
            isinstance(item.get("md5"), str) and len(item["md5"]) == 32
            for item in audio_tracks) and
        len(payloads) == content.get("summary", {}).get("cataloged_payloads") and
        all(item.get("container") and item.get("container_sha256") and
            item.get("inventory_sha256") and item.get("expanded_sha256")
            for item in payloads) and
        {item.get("logical_path") for item in exclusions} == expected_exclusions and
        policy.get("media", {}).get("source") == "verified-external-multi-file-cue-bin" and
        policy.get("media", {}).get("redistribute_source_media") is False)

    executable_payloads = [item for item in payloads if item.get("classification") in {
        "authenticated-main-executable", "authenticated-runtime-executable"}]
    payload_mappings = {item.get("static_mapping") for item in executable_payloads}
    images = shards.get("images", [])
    shard_names = {item.get("name") for item in images}
    static_ok = (
        len(executable_payloads) == content.get("summary", {}).get(
            "authenticated_executable_payloads") == 2 and
        payload_mappings == shard_names == {"main-exe", "nfe-entry5-route-variant"} and
        shards.get("compiled_instruction_count") == 135829 and
        shards.get("unresolved_cfi_site_count") == 0 and
        all(item.get("authorized_instruction_count") == item.get("compiled_instruction_count") and
            item.get("excluded_instruction_count") == 0 and item.get("compiled_region_count", 0) > 0
            for item in images) and
        content.get("additional_executable_candidates") == [])

    evidence_by_route: dict[str, list[dict[str, str]]] = {}
    for path in sorted((item.resolve() for item in behavioral_evidence), key=lambda item: item.as_posix()):
        document = _load(path)
        if (document.get("schema") != "wipeout3se.clean-aot.behavioral-execution.v1" or
                document.get("status") != "passed" or
                document.get("game_id") != policy["game_id"] or
                document.get("runtime") != "installed-compiler-free-product" or
                document.get("external_media_verified") is not True):
            raise ContentCoverageError("behavioral evidence is not eligible clean-product execution")
        for route in document.get("routes", []):
            if not isinstance(route, str):
                raise ContentCoverageError("behavioral evidence route is malformed")
            evidence_by_route.setdefault(route, []).append({"path": path.as_posix(), "sha256": _sha(path)})

    required: list[dict[str, Any]] = []
    routes = policy.get("behavioral_routes", {})
    for category in sorted(routes):
        values = routes[category]
        if not isinstance(values, list) or not values or len(values) != len(set(values)):
            raise ContentCoverageError("behavioral policy category is empty or duplicated")
        for value in sorted(values):
            route_id = f"{category}:{value}"
            receipts = evidence_by_route.get(route_id, [])
            required.append({"id": route_id, "status": "executed" if receipts else "unexecuted",
                             "evidence": receipts})
    required_ids = {item["id"] for item in required}
    unknown_routes = sorted(set(evidence_by_route) - required_ids)
    if unknown_routes:
        raise ContentCoverageError("behavioral evidence names routes outside the policy ledger")
    executed = sum(item["status"] == "executed" for item in required)
    behavioral_ok = executed == len(required)

    receipt = {
        "schema": SCHEMA, "status": "pass" if media_ok and static_ok and behavioral_ok
        else "incomplete-fail-closed", "game_ready": False,
        "inputs": {"content_sha256": _sha(content_path), "game_shards_sha256": _sha(shards_path),
                   "media_manifest_sha256": _sha(media_manifest_path),
                   "policy_sha256": _sha(policy_path)},
        "media_availability": {"status": "pass" if media_ok else "fail",
            "source": "verified-external-multi-file-cue-bin", "redistributed": False,
            "disc_tracks": 14, "data_payloads": len(payloads), "cdda_tracks": len(audio_tracks),
            "runtime_contract": policy.get("media", {}).get("runtime_access")},
        "static_executable_coverage": {"status": "pass" if static_ok else "fail",
            "authenticated_images": sorted(shard_names),
            "compiled_instructions": shards.get("compiled_instruction_count"),
            "compiled_regions": shards.get("compiled_region_count"),
            "unresolved_cfi_sites": shards.get("unresolved_cfi_site_count")},
        "behavioral_execution": {"status": "pass" if behavioral_ok else "fail",
            "required": len(required), "executed": executed,
            "unexecuted": len(required) - executed, "routes": required}}
    if receipt_path is not None:
        write_readiness_receipt(receipt_path, receipt)
    return receipt
