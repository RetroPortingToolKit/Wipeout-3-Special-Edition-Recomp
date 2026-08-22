"""Strict modern-PC release packaging and deterministic validation lane."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Sequence

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.cleanroom.gate_common import GateError, scan_link_map, scan_source


class ReleaseError(RuntimeError):
    """Release input or validation failure."""


SCHEMA = "wipeout3se.clean-aot.release.v1"
SETUP_SCHEMA = "wipeout3se.clean-aot.first-run-setup.v1"
CONFIG_SCHEMA = "wipeout3se.clean-aot.config.v1"
INSTALL_SCHEMA = "wipeout3se.clean-aot.install.v1"
CARD_SIZE = 128 * 1024
FORBIDDEN_NAMES = (
    "bios", "scph", "track01", ".cue", ".iso", ".bin", "compiler",
    "oracle", "interpreter", "legacy", "ghidra", "duckstation", "goosestation",
)


def _canonical(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":")) + "\n").encode("utf-8")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _safe_relative(value: str) -> Path:
    path = Path(value)
    if (not value or path.is_absolute() or path.drive or any(
            part in {"", ".", ".."} for part in path.parts)):
        raise ReleaseError(f"unsafe release-relative path: {value!r}")
    return path


def _reject_leaking_name(path: Path) -> None:
    folded = path.as_posix().casefold()
    if any(token in folded for token in FORBIDDEN_NAMES):
        raise ReleaseError(f"forbidden release payload name: {path.as_posix()}")


def _copy_payload(source: Path, destination: Path) -> None:
    if not source.is_file() or source.is_symlink():
        raise ReleaseError(f"release payload is not a regular file: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def _profile_hashes(profile: Path | None) -> set[str]:
    if profile is None:
        return set()
    try:
        value = json.loads(profile.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ReleaseError("cannot read release leakage profile") from error
    result: set[str] = set()
    def visit(item: Any) -> None:
        if isinstance(item, dict):
            for key, child in item.items():
                if key.endswith("sha256") and isinstance(child, str) and len(child) == 64:
                    result.add(child.casefold())
                visit(child)
        elif isinstance(item, list):
            for child in item:
                visit(child)
    visit(value)
    return result


def default_setup_contract(executable_name: str) -> dict[str, Any]:
    return {
        "schema": SETUP_SCHEMA,
        "mode": "one-time-first-run",
        "launcher": f"${{PACKAGE_ROOT}}/bin/{executable_name}",
        "user_data_root": "${USER_DATA}/Wipeout3SE",
        "required_imports": [
            {"id": "retail-disc", "kind": "cue-bin", "retained": "never"},
            {"id": "retail-bios", "kind": "bios", "retained": "external-path-only"},
            {"id": "pal-patch", "kind": "xdelta3", "retained": "never"},
        ],
        "generated_user_data": [
            "receipts/active-setup.json", "setups/<version>/receipt.json",
            "setups/<version>/finalize-receipt.json", "setups/<version>/catalog.json",
            "setups/<version>/artifacts", "setups/<version>/runtime/user-bios.path",
        ],
        "package_must_not_contain": ["media", "bios", "setup receipts", "compiler"],
        "unknown_identity": "reject-before-runtime-initialization",
        "setup_command": "build_product.py first-run --cue <WIP3OUT_SE.cue> --bios <SCPH1001.bin> --patch <pal_track01.xdelta3> --xdelta3 <xdelta3.exe> --compiler <clean-aot-compiler.exe> --regions <product-regions.json> --artifacts <artifacts.json> --artifact-root <native-build> --user-data <USER_DATA>",
        "subsequent_launch": "Wipeout3SE.exe --user-data <USER_DATA>",
        "activation": "compile-finalize-copy-generated-artifacts-atomic-switch",
    }


def default_config() -> dict[str, Any]:
    return {
        "schema": CONFIG_SCHEMA,
        "save": {"root": "${USER_DATA}/Wipeout3SE/saves", "card_slots": 2,
                 "atomic_replace": True},
        "input": {"profile": "modern-gamepad", "deadzone": 0.12,
                  "hotplug": True, "background_input": False,
                  "mapping": {"cross": "south", "circle": "east",
                              "square": "west", "triangle": "north",
                              "l1": "left-shoulder", "r1": "right-shoulder",
                              "l2": "left-trigger", "r2": "right-trigger",
                              "start": "start", "select": "back"}},
        "audio": {"sample_rate_hz": 44100, "channels": 2,
                  "buffer_frames": 882, "exclusive_mode": False,
                  "underrun": "count-and-resume-next-batch", "max_queued_frames": 3528},
        "video": {"update_rate_hz": 100, "fullscreen": "borderless",
                  "aspect": "auto-ultrawide", "render_scale": "native-4k",
                  "window_width": 3840, "window_height": 2160},
    }


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".new")
    if temporary.exists():
        if temporary.is_symlink() or not temporary.is_file():
            raise ReleaseError("unsafe stale atomic-write temporary")
        temporary.unlink()
    with temporary.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def initialize_install(package: Path, user_data: Path) -> dict[str, Any]:
    """Create durable per-user state without asserting that media is authorized."""
    validation = validate_release(package)
    user_data = user_data.resolve(strict=False)
    if user_data.exists() and user_data.is_symlink():
        raise ReleaseError("user data root must not be a symlink")
    config_path = user_data / "config.json"
    if not config_path.exists():
        _atomic_write(config_path, _canonical(default_config()))
    try:
        installed_config = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ReleaseError("installed configuration is malformed") from error
    _validate_config(installed_config)
    for slot in (1, 2):
        card = user_data / "saves" / f"card{slot}.mcd"
        if not card.exists():
            _atomic_write(card, bytes(CARD_SIZE))
        if card.is_symlink() or card.stat().st_size != CARD_SIZE:
            raise ReleaseError(f"memory card slot {slot} is malformed")
    receipt = {
        "schema": INSTALL_SCHEMA,
        "status": "awaiting-authenticated-media-import",
        "package_manifest_sha256": validation["manifest_sha256"],
        "configuration_sha256": _sha256(config_path),
        "memory_cards": [
            {"slot": slot, "size": CARD_SIZE,
             "sha256": _sha256(user_data / "saves" / f"card{slot}.mcd")}
            for slot in (1, 2)
        ],
        "next_action": "select lawful retail disc, BIOS, and PAL patch",
        "game_ready": False,
    }
    _atomic_write(user_data / "receipts" / "install.json", _canonical(receipt))
    return receipt


def portable_launcher() -> bytes:
    return ("@echo off\r\nsetlocal\r\n"
            "set \"PACKAGE_ROOT=%~dp0\"\r\n"
            "if not defined LOCALAPPDATA exit /b 64\r\n"
            "set \"USER_DATA=%LOCALAPPDATA%\\Wipeout3SE\"\r\n"
            "if not exist \"%USER_DATA%\\receipts\\active-setup.json\" (\r\n"
            "  echo First run requires your WipEout 3 SE CUE/BIN set and PlayStation BIOS. 1>&2\r\n"
            "  echo Use the authenticated first-run setup command documented in policy\\first-run-setup.json. 1>&2\r\n"
            "  exit /b 78\r\n"
            ")\r\n"
            "\"%PACKAGE_ROOT%bin\\Wipeout3SE.exe\" --user-data \"%USER_DATA%\"\r\n"
            "exit /b %ERRORLEVEL%\r\n").encode("ascii")


def create_release(*, app: Path, sdl: Path, artifacts: Sequence[tuple[str, Path]],
                   output: Path, root: Path, link_map: Path,
                   sources: Sequence[Path], allowlist: Path | None = None,
                   leakage_profile: Path | None = None) -> dict[str, Any]:
    """Create an atomic, relocatable package from explicit clean shipping inputs."""
    root = root.resolve(strict=True)
    output = output.resolve(strict=False)
    if output.exists():
        raise ReleaseError(f"release output already exists: {output}")
    try:
        source_receipt = scan_source(sources, root=root, allowlist_path=allowlist)
        link_receipt = scan_link_map(link_map, root=root, allowlist_path=allowlist)
    except GateError as error:
        raise ReleaseError(str(error)) from error
    if source_receipt["status"] != "pass" or link_receipt["status"] != "pass":
        raise ReleaseError("release source or link-map isolation gate failed")
    if not artifacts:
        raise ReleaseError("release requires at least one generated clean artifact")
    if sdl.name.casefold() != "sdl3.dll":
        raise ReleaseError("Windows release SDL payload must be named SDL3.dll")
    forbidden_hashes = _profile_hashes(leakage_profile)

    executable_name = "Wipeout3SE.exe" if app.suffix.casefold() == ".exe" else "Wipeout3SE"
    payloads: list[tuple[str, Path, str]] = [
        (f"bin/{executable_name}", app, "application"),
        (f"bin/{sdl.name}", sdl, "sdl-runtime"),
    ]
    for relative, source in artifacts:
        safe = _safe_relative(relative)
        _reject_leaking_name(safe)
        payloads.append((f"artifacts/{safe.as_posix()}", source, "generated-clean-artifact"))
    destinations = [item[0].casefold() for item in payloads]
    if len(destinations) != len(set(destinations)):
        raise ReleaseError("portable release payload path collision")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}-", dir=output.parent))
    try:
        entries: list[dict[str, Any]] = []
        for relative, source, role in payloads:
            relative_path = _safe_relative(relative)
            _reject_leaking_name(relative_path)
            destination = temporary / relative_path
            _copy_payload(source.resolve(strict=True), destination)
            payload_hash = _sha256(destination)
            if payload_hash.casefold() in forbidden_hashes:
                raise ReleaseError(
                    f"release payload matches a forbidden media/BIOS/input identity: {relative}")
            entries.append({"path": relative_path.as_posix(), "role": role,
                            "size": destination.stat().st_size,
                            "sha256": payload_hash})
        policy = temporary / "policy"
        policy.mkdir()
        (policy / "first-run-setup.json").write_bytes(
            _canonical(default_setup_contract(executable_name)))
        (policy / "default-config.json").write_bytes(_canonical(default_config()))
        launcher = temporary / "Wipeout3SE.cmd"
        launcher.write_bytes(portable_launcher())
        for path, role in ((policy / "first-run-setup.json", "setup-contract"),
                           (policy / "default-config.json", "configuration-defaults"),
                           (launcher, "portable-launcher")):
            entries.append({"path": path.relative_to(temporary).as_posix(), "role": role,
                            "size": path.stat().st_size, "sha256": _sha256(path)})
        entries.sort(key=lambda item: item["path"])
        manifest = {"schema": SCHEMA, "status": "packaged-not-game-ready",
                    "entrypoint": f"bin/{executable_name}", "files": entries,
                    "isolation": {"source_receipt_sha256": hashlib.sha256(
                        _canonical(source_receipt)).hexdigest(),
                        "link_map_receipt_sha256": hashlib.sha256(
                            _canonical(link_receipt)).hexdigest()}}
        (temporary / "release-manifest.json").write_bytes(_canonical(manifest))
        validate_release(temporary)
        os.replace(temporary, output)
        return manifest
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def validate_release(package: Path) -> dict[str, Any]:
    package = package.resolve(strict=True)
    manifest_path = package / "release-manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ReleaseError("missing or malformed release manifest") from error
    if manifest.get("schema") != SCHEMA or manifest.get("status") != "packaged-not-game-ready":
        raise ReleaseError("release manifest schema/status mismatch")
    listed: set[str] = {"release-manifest.json"}
    for entry in manifest.get("files", []):
        relative = _safe_relative(entry.get("path", ""))
        _reject_leaking_name(relative)
        if relative.as_posix() in listed:
            raise ReleaseError("duplicate release manifest entry")
        listed.add(relative.as_posix())
        path = package / relative
        if (not path.is_file() or path.is_symlink() or path.stat().st_size != entry.get("size")
                or _sha256(path) != entry.get("sha256")):
            raise ReleaseError(f"release payload measurement mismatch: {relative}")
    actual = {path.relative_to(package).as_posix() for path in package.rglob("*") if path.is_file()}
    if actual != listed:
        raise ReleaseError("release contains unmanifested or missing files")
    for policy_name in ("first-run-setup.json", "default-config.json"):
        text = (package / "policy" / policy_name).read_text(encoding="utf-8")
        if str(package) in text or "C:\\" in text or "/home/" in text:
            raise ReleaseError("release policy embeds a machine-specific path")
    setup = json.loads((package / "policy" / "first-run-setup.json").read_text(
        encoding="utf-8"))
    config = json.loads((package / "policy" / "default-config.json").read_text(
        encoding="utf-8"))
    if (setup.get("schema") != SETUP_SCHEMA or setup.get("mode") != "one-time-first-run"
            or setup.get("unknown_identity") != "reject-before-runtime-initialization"
            or len(setup.get("required_imports", [])) != 3
            or "first-run" not in setup.get("setup_command", "")
            or setup.get("activation") != "compile-finalize-copy-generated-artifacts-atomic-switch"
            or not str(setup.get("user_data_root", "")).startswith("${USER_DATA}/")):
        raise ReleaseError("first-run setup UX contract is invalid")
    _validate_config(config)
    launcher = (package / "Wipeout3SE.cmd").read_bytes()
    if launcher != portable_launcher() or b"%~dp0" not in launcher:
        raise ReleaseError("portable launcher contract is invalid")
    return {"schema": SCHEMA, "status": "validated", "files": len(listed),
            "manifest_sha256": _sha256(manifest_path)}


def _validate_config(config: dict[str, Any]) -> None:
    if (config.get("schema") != CONFIG_SCHEMA
            or config.get("audio", {}).get("sample_rate_hz") != 44100
            or config.get("audio", {}).get("channels") != 2
            or not 441 <= config.get("audio", {}).get("buffer_frames", 0) <= 8192
            or config.get("video", {}).get("update_rate_hz") != 100
            or config.get("audio", {}).get("underrun") != "count-and-resume-next-batch"
            or config.get("audio", {}).get("max_queued_frames", 0) < 882
            or not 0.0 <= config.get("input", {}).get("deadzone", -1.0) <= 0.5
            or config.get("input", {}).get("profile") != "modern-gamepad"
            or config.get("save", {}).get("card_slots") not in {1, 2}
            or not str(config.get("save", {}).get("root", "")).startswith("${USER_DATA}/")):
        raise ReleaseError("save/input/audio/video configuration boundary is invalid")


def run_soak(command: Sequence[str], *, iterations: int, timeout_seconds: float,
             max_p95_ms: float, cwd: Path | None = None,
             receipt_path: Path | None = None) -> dict[str, Any]:
    if iterations < 3 or iterations > 10000 or timeout_seconds <= 0 or max_p95_ms <= 0:
        raise ReleaseError("invalid bounded soak parameters")
    environment = {"PATH": os.environ.get("PATH", ""), "LANG": "C", "LC_ALL": "C",
                   "TZ": "UTC", "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}
    for name in ("SYSTEMROOT", "WINDIR", "TEMP", "TMP"):
        if name in os.environ:
            environment[name] = os.environ[name]
    environment["PYTHONHASHSEED"] = "0"
    digests: list[str] = []
    elapsed_ms: list[float] = []
    for _ in range(iterations):
        started = time.perf_counter_ns()
        try:
            completed = subprocess.run(command, cwd=cwd, env=environment, check=False,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, timeout=timeout_seconds)
        except subprocess.TimeoutExpired as error:
            raise ReleaseError("soak command timed out") from error
        elapsed_ms.append((time.perf_counter_ns() - started) / 1_000_000.0)
        if completed.returncode != 0:
            raise ReleaseError(f"soak command failed with exit code {completed.returncode}")
        digests.append(hashlib.sha256(completed.stdout + b"\0" + completed.stderr).hexdigest())
    if len(set(digests)) != 1:
        raise ReleaseError("soak command output is nondeterministic")
    ordered = sorted(elapsed_ms)
    p95 = ordered[max(0, (95 * len(ordered) + 99) // 100 - 1)]
    if p95 > max_p95_ms:
        raise ReleaseError(f"soak p95 {p95:.3f} ms exceeds {max_p95_ms:.3f} ms")
    receipt = {"schema": "wipeout3se.clean-aot.soak.v1", "status": "pass",
            "iterations": iterations, "output_sha256": digests[0],
            "command_sha256": hashlib.sha256(_canonical(list(command))).hexdigest(),
            "timing_ms": {"min": min(elapsed_ms), "median": statistics.median(elapsed_ms),
                          "p95": p95, "max": max(elapsed_ms), "limit_p95": max_p95_ms}}
    if receipt_path is not None:
        _atomic_write(receipt_path.resolve(strict=False), _canonical(receipt))
    return receipt


def run_installed_product_soak(package: Path, *, bios: Path, profile: str,
                               updates: int, iterations: int, timeout_seconds: float,
                               max_p95_ms: float, receipt_path: Path | None = None) -> dict[str, Any]:
    """Exercise the installed product's bounded 100-Hz host workload."""
    if profile not in {"4k-headless", "4k-hidden", "5120x2160-headless",
                       "5120x2160-hidden"}:
        raise ReleaseError("unknown installed-product workload profile")
    if not 1 <= updates <= 1000:
        raise ReleaseError("installed-product workload update bound is invalid")
    if not 2 <= iterations <= 1000 or timeout_seconds <= 0 or max_p95_ms <= 0:
        raise ReleaseError("installed-product soak bound is invalid")
    validate_release(package)
    command = [str(package.resolve(strict=True) / "bin" / "Wipeout3SE.exe"),
               "--bios", str(bios.resolve(strict=True)), "--smoke-hidden",
               "--workload-profile", profile, "--workload-updates", str(updates)]
    environment = {"PATH": os.environ.get("PATH", ""), "LANG": "C", "LC_ALL": "C",
                   "TZ": "UTC", "PYTHONHASHSEED": "0"}
    for name in ("SYSTEMROOT", "WINDIR", "TEMP", "TMP", "LOCALAPPDATA"):
        if name in os.environ: environment[name] = os.environ[name]
    elapsed: list[float] = []
    outputs: list[bytes] = []
    frontier: dict[str, Any] | None = None
    for _ in range(iterations):
        started = time.perf_counter_ns()
        try:
            completed = subprocess.run(command, cwd=package / "bin", env=environment,
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=timeout_seconds, check=False)
        except subprocess.TimeoutExpired as error:
            raise ReleaseError("installed-product soak timed out") from error
        elapsed.append((time.perf_counter_ns() - started) / 1_000_000.0)
        if completed.returncode != 0:
            raise ReleaseError("installed-product workload failed closed")
        outputs.append(completed.stdout + b"\0" + completed.stderr)
        try: current = json.loads(completed.stdout.decode("utf-8").strip().splitlines()[-1])
        except (UnicodeError, ValueError, IndexError) as error:
            raise ReleaseError("installed-product workload emitted no metrics") from error
        workload = current.get("workload", {})
        if (current.get("stage") != "bios-slice-stopped" or
                workload.get("profile") != profile or workload.get("rate_hz") != 100 or
                workload.get("updates") != updates or
                workload.get("frames_submitted") != updates or
                workload.get("audio_batches_submitted") != updates or
                workload.get("audio_sample_frames") != updates * 441 or
                workload.get("pacing_span_ns") != updates * 10_000_000 or
                workload.get("gpu_submission_failures") != 0 or
                not 0 <= workload.get("gpu_deferred_updates", -1) <= updates or
                workload.get("audio_underruns") != 0 or
                workload.get("pacing_late_updates") != 0):
            raise ReleaseError("installed-product workload metrics exceeded thresholds")
        frontier = current
    digests = {hashlib.sha256(output).hexdigest() for output in outputs}
    if len(digests) != 1: raise ReleaseError("installed-product workload is nondeterministic")
    ordered = sorted(elapsed)
    p95 = ordered[max(0, (95 * len(ordered) + 99) // 100 - 1)]
    if p95 > max_p95_ms: raise ReleaseError("installed-product workload p95 exceeded threshold")
    receipt = {"schema": "wipeout3se.clean-aot.installed-soak.v1", "status": "passed-not-game-ready",
        "game_ready": False, "profile": profile, "updates_per_iteration": updates,
        "iterations": iterations, "metrics": frontier["workload"],
        "cpu": {"bios_instructions_executed": frontier["bios_instructions_executed"],
                "generated_dispatches": frontier["bios_generated_dispatches"],
                "process_p95_ms": p95},
        "output_sha256": next(iter(digests)),
        "timing_ms": {"min": min(elapsed), "median": statistics.median(elapsed),
                      "p95": p95, "max": max(elapsed), "limit_p95": max_p95_ms}}
    if receipt_path is not None: _atomic_write(receipt_path.resolve(strict=False), _canonical(receipt))
    return receipt
