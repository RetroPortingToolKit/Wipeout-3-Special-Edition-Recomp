"""Deterministic installed-product behavioral scenario runner.

The runner is deliberately external to the runtime.  It publishes coverage
evidence only when Wipeout3SE.exe returns an authenticated observation with
hash-bound trace, frame, audio, and save captures after real game boot.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path
from typing import Any, Sequence

if __package__ in {None, ""}:
    from release_lane import validate_release  # type: ignore[import-not-found]
    from release_readiness import write_readiness_receipt  # type: ignore[import-not-found]
else:
    from .release_lane import validate_release
    from .release_readiness import write_readiness_receipt

PLAN_SCHEMA = "wipeout3se.clean-aot.behavioral-scenario-plan.v1"
OBSERVATION_SCHEMA = "wipeout3se.clean-aot.behavioral-observation.v1"
EVIDENCE_SCHEMA = "wipeout3se.clean-aot.behavioral-execution.v1"
CAPTURE_KINDS = ("static_trace", "frame", "audio", "save")


class BehavioralRunnerError(ValueError):
    """A scenario or observation failed the authenticated evidence contract."""


def _load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise BehavioralRunnerError(f"missing or malformed JSON: {path}") from error
    if not isinstance(value, dict):
        raise BehavioralRunnerError(f"JSON root is not an object: {path}")
    return value


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _is_sha256(value: Any) -> bool:
    return (isinstance(value, str) and len(value) == 64 and
            all(character in "0123456789abcdef" for character in value))


def build_scenario_plan(policy_path: Path, receipt_path: Path | None = None) -> dict[str, Any]:
    policy = _load(policy_path)
    if policy.get("schema") != "wipeout3se.content-coverage-policy.v1":
        raise BehavioralRunnerError("behavioral policy schema mismatch")
    scenarios: list[dict[str, Any]] = []
    for category, values in sorted(policy.get("behavioral_routes", {}).items()):
        if not isinstance(values, list) or not values:
            raise BehavioralRunnerError("behavioral policy route category is invalid")
        for value in sorted(values):
            route = f"{category}:{value}"
            actions: list[dict[str, Any]] = [
                {"at_update": 0, "op": "await-authenticated-game-boot"},
                {"at_update": 1, "op": "reset-input"},
                {"at_update": 2, "op": "navigate-main-menu"},
            ]
            if category in {"modes", "tracks", "teams", "ships"}:
                actions.extend([
                    {"at_update": 3, "op": "select", "category": category, "value": value},
                    {"at_update": 4, "op": "start-race"},
                    {"at_update": 5, "op": "run-bounded-race", "updates": 3000},
                ])
            elif category == "menus":
                actions.append({"at_update": 3, "op": "navigate-menu", "value": value})
            elif category == "fmvs":
                actions.append({"at_update": 3, "op": "play-fmv", "value": value})
            elif category == "audio_routes":
                actions.append({"at_update": 3, "op": "exercise-audio-route", "value": value})
            else:
                raise BehavioralRunnerError(f"unsupported route category: {category}")
            actions.append({"at_update": actions[-1]["at_update"] + 1,
                            "op": "capture-trace-frame-audio-save"})
            scenarios.append({"route": route, "seed": 0, "updates_hz": 100,
                              "actions": actions})
    routes = [item["route"] for item in scenarios]
    expected_count = policy.get("required_route_count", len(routes))
    if (not isinstance(expected_count, int) or expected_count <= 0 or
            len(routes) != expected_count or len(routes) != len(set(routes))):
        raise BehavioralRunnerError(
            f"scenario plan must cover exactly {expected_count} unique routes")
    document = {"schema": PLAN_SCHEMA, "game_id": policy.get("game_id"),
                "policy_sha256": _sha(policy_path), "scenario_count": len(scenarios),
                "scenarios": scenarios}
    if receipt_path:
        write_readiness_receipt(receipt_path, document)
    return document


def _capture(root: Path, item: Any, kind: str) -> dict[str, Any]:
    if not isinstance(item, dict) or set(item) != {"path", "sha256", "bytes"}:
        raise BehavioralRunnerError(f"{kind} capture contract is malformed")
    relative = Path(str(item["path"]))
    if relative.is_absolute() or ".." in relative.parts:
        raise BehavioralRunnerError(f"{kind} capture escapes observation directory")
    path = (root / relative).resolve()
    if (not isinstance(item["bytes"], int) or item["bytes"] <= 0 or
            not _is_sha256(item["sha256"]) or not path.is_file() or
            path.stat().st_size != item["bytes"] or _sha(path) != item["sha256"]):
        raise BehavioralRunnerError(f"{kind} capture identity mismatch")
    return {"path": relative.as_posix(), "sha256": item["sha256"], "bytes": item["bytes"]}


def authenticate_observation(*, observation_path: Path, scenario: dict[str, Any],
                             scenario_sha256: str, package_manifest_sha256: str,
                             executable_sha256: str, media_manifest_sha256: str,
                             game_id: str) -> dict[str, Any]:
    observation = _load(observation_path)
    if (observation.get("schema") != OBSERVATION_SCHEMA or
            observation.get("status") != "passed" or observation.get("game_id") != game_id or
            observation.get("route") != scenario.get("route") or
            observation.get("scenario_sha256") != scenario_sha256 or
            observation.get("package_manifest_sha256") != package_manifest_sha256 or
            observation.get("executable_sha256") != executable_sha256):
        raise BehavioralRunnerError("observation identity mismatch")
    boot = observation.get("game_boot", {})
    if (boot.get("status") != "authenticated-game-running" or
            not isinstance(boot.get("game_instructions"), int) or
            boot["game_instructions"] <= 0 or
            not _is_sha256(boot.get("authenticated_epoch_sha256"))):
        raise BehavioralRunnerError("scenario is dormant until authenticated game boot")
    media = observation.get("external_media", {})
    if (media.get("verified") is not True or
            media.get("manifest_sha256") != media_manifest_sha256):
        raise BehavioralRunnerError("external media authentication mismatch")
    captures = observation.get("captures", {})
    checked = {kind: _capture(observation_path.parent, captures.get(kind), kind)
               for kind in CAPTURE_KINDS}
    return {"route": scenario["route"], "game_instructions": boot["game_instructions"],
            "authenticated_epoch_sha256": boot["authenticated_epoch_sha256"],
            "captures": checked}


def run_behavioral_scenarios(*, package: Path, policy_path: Path, media_manifest_path: Path,
                             routes: Sequence[str] = (), timeout_seconds: float = 120.0,
                             receipt_path: Path | None = None) -> dict[str, Any]:
    package = package.resolve()
    validation = validate_release(package)
    manifest_hash = validation["manifest_sha256"]
    executable = package / "bin" / "Wipeout3SE.exe"
    executable_hash = _sha(executable)
    plan = build_scenario_plan(policy_path)
    selected = set(routes) if routes else {item["route"] for item in plan["scenarios"]}
    known = {item["route"] for item in plan["scenarios"]}
    if not selected or not selected <= known:
        raise BehavioralRunnerError("requested route is absent from scenario plan")
    completed: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="wipeout3se-behavior-") as temporary:
        root = Path(temporary)
        for index, scenario in enumerate(item for item in plan["scenarios"]
                                         if item["route"] in selected):
            scenario_path = root / f"scenario-{index:02}.json"
            scenario_path.write_bytes(_canonical(scenario))
            observation_path = root / f"observation-{index:02}.json"
            environment = os.environ.copy()
            environment.update({"TZ": "UTC", "LC_ALL": "C", "SDL_AUDIODRIVER": "dummy"})
            try:
                process = subprocess.run([
                    str(executable), "--behavioral-scenario", str(scenario_path),
                    "--behavioral-observation", str(observation_path), "--headless"],
                    cwd=package, env=environment, stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout_seconds,
                    check=False)
            except subprocess.TimeoutExpired as error:
                raise BehavioralRunnerError("behavioral scenario timed out before proof") from error
            if process.returncode != 0 or not observation_path.is_file():
                raise BehavioralRunnerError("installed product produced no behavioral proof")
            completed.append(authenticate_observation(
                observation_path=observation_path, scenario=scenario,
                scenario_sha256=_sha(scenario_path), package_manifest_sha256=manifest_hash,
                executable_sha256=executable_hash, media_manifest_sha256=_sha(media_manifest_path),
                game_id=plan["game_id"]))
    document = {"schema": EVIDENCE_SCHEMA, "status": "passed", "game_id": plan["game_id"],
                "runtime": "installed-compiler-free-product", "external_media_verified": True,
                "package_manifest_sha256": manifest_hash, "executable_sha256": executable_hash,
                "scenario_plan_sha256": hashlib.sha256(_canonical(plan)).hexdigest(),
                "media_manifest_sha256": _sha(media_manifest_path),
                "routes": sorted(item["route"] for item in completed), "executions": completed}
    if receipt_path:
        write_readiness_receipt(receipt_path, document)
    return document
