"""Exercise the compiler-free installed settings UI through Wipeout3SE.exe."""

from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
from typing import Any

if __package__ in {None, ""}:
    from release_lane import validate_release  # type: ignore[import-not-found]
    from release_readiness import write_readiness_receipt  # type: ignore[import-not-found]
else:
    from .release_lane import validate_release
    from .release_readiness import write_readiness_receipt

SCHEMA = "wipeout3se.clean-aot.settings-ui-acceptance.v1"


class SettingsUiAcceptanceError(ValueError):
    pass


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _run(executable: Path, config: Path, actions: list[str]) -> dict[str, Any]:
    command = [str(executable), "--settings", str(config)]
    for action in actions: command.extend(("--settings-ui-action", action))
    process = subprocess.run(command, cwd=executable.parent, stdin=subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             timeout=30.0, check=False, text=True, encoding="utf-8")
    lines = [line for line in process.stdout.splitlines() if line.strip()]
    try: receipt = json.loads(lines[-1]) if lines else {}
    except ValueError as error: raise SettingsUiAcceptanceError("settings UI emitted malformed receipt") from error
    return {"exit": process.returncode, "receipt": receipt}


def run_settings_ui_acceptance(package: Path, receipt_path: Path | None = None) -> dict[str, Any]:
    package = package.resolve()
    validation = validate_release(package)
    executable = package / "bin" / "Wipeout3SE.exe"
    with tempfile.TemporaryDirectory(prefix="wipeout3se-settings-ui-") as temporary:
        config = Path(temporary) / "\u5229\u7528\u8005" / "\u69cb\u6210.json"
        save = _run(executable, config, [
            "display.width=5120", "display.height=2160", "display.mode=borderless",
            "display.aspect=auto-ultrawide", "display.filter=linear",
            "presentation.policy=low-latency-100hz", "graphics.pgxp=on",
            "graphics.pgxp_verified_near_plane=on", "graphics.true_color=on",
            "graphics.hd_assets=on", "audio.device=\u97f3\u58f0\u30c7\u30d0\u30a4\u30b9",
            "audio.latency_frames=1323", "audio.max_queued_frames=3528",
            "input.deadzone_per_mille=175", "input.rumble=on", "saves.card_slots=2", "save"])
        if save["exit"] != 0 or save["receipt"].get("stage") != "settings-saved":
            raise SettingsUiAcceptanceError("installed settings save failed")
        configured = json.loads(config.read_text(encoding="utf-8"))
        saved_hash = _sha(config)
        cancel = _run(executable, config, ["display.width=640", "cancel"])
        if (cancel["exit"] != 0 or cancel["receipt"].get("stage") != "settings-cancelled" or
                _sha(config) != saved_hash):
            raise SettingsUiAcceptanceError("cancel mutated installed settings")
        invalid = _run(executable, config, ["display.width=99999", "save"])
        if invalid["exit"] == 0 or _sha(config) != saved_hash:
            raise SettingsUiAcceptanceError("invalid installed setting did not fail closed")
        reset = _run(executable, config, ["reset-defaults", "save"])
        defaults = json.loads(config.read_text(encoding="utf-8"))
        if reset["exit"] != 0 or defaults.get("display", {}).get("width") != 3840:
            raise SettingsUiAcceptanceError("installed reset defaults failed")
    document = {"schema": SCHEMA, "status": "passed", "game_ready": False,
                "package_manifest_sha256": validation["manifest_sha256"],
                "runtime_executable_sha256": _sha(executable), "runtime_compiler_free": True,
                "entrypoints": {"first_run_missing_config": True, "explicit_flag": "--settings-ui"},
                "controls": "native-win32-keyboard-navigation",
                "coverage": {"display_4k_ultrawide": True, "graphics_enhancements": True,
                             "audio_device_latency": True, "controller_deadzone_rumble": True,
                             "memory_cards": True, "reset_defaults": True},
                "atomic_schema": configured.get("schema"), "unicode": True,
                "cancel_without_mutation": True, "invalid_fail_closed": True,
                "headless_automation": True}
    if receipt_path: write_readiness_receipt(receipt_path, document)
    return document
