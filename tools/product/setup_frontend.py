"""Native Windows first-run frontend; frozen as Wipeout3SE-Setup.exe."""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import subprocess
import sys
from ctypes import wintypes
from pathlib import Path
from typing import Callable

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.product.install_lane import orchestrate_first_run
from tools.product.installer_lane import install_runtime


class SetupCancelled(RuntimeError):
    pass


def select_file(title: str, pattern: str) -> Path | None:
    if sys.platform != "win32":
        raise RuntimeError("native setup selectors require Windows")
    class OPENFILENAMEW(ctypes.Structure):
        _fields_ = [("lStructSize", wintypes.DWORD), ("hwndOwner", wintypes.HWND),
                    ("hInstance", wintypes.HINSTANCE), ("lpstrFilter", wintypes.LPCWSTR),
                    ("lpstrCustomFilter", wintypes.LPWSTR), ("nMaxCustFilter", wintypes.DWORD),
                    ("nFilterIndex", wintypes.DWORD), ("lpstrFile", wintypes.LPWSTR),
                    ("nMaxFile", wintypes.DWORD), ("lpstrFileTitle", wintypes.LPWSTR),
                    ("nMaxFileTitle", wintypes.DWORD), ("lpstrInitialDir", wintypes.LPCWSTR),
                    ("lpstrTitle", wintypes.LPCWSTR), ("Flags", wintypes.DWORD),
                    ("nFileOffset", wintypes.WORD), ("nFileExtension", wintypes.WORD),
                    ("lpstrDefExt", wintypes.LPCWSTR), ("lCustData", wintypes.LPARAM),
                    ("lpfnHook", ctypes.c_void_p), ("lpTemplateName", wintypes.LPCWSTR),
                    ("pvReserved", ctypes.c_void_p), ("dwReserved", wintypes.DWORD),
                    ("FlagsEx", wintypes.DWORD)]
    buffer = ctypes.create_unicode_buffer(32768)
    spec = OPENFILENAMEW()
    spec.lStructSize = ctypes.sizeof(spec); spec.lpstrFile = buffer; spec.nMaxFile = len(buffer)
    spec.lpstrFilter = pattern; spec.lpstrTitle = title
    spec.Flags = 0x00001000 | 0x00000800 | 0x00000008
    if not ctypes.windll.comdlg32.GetOpenFileNameW(ctypes.byref(spec)):
        return None
    return Path(buffer.value)


def run_setup(package_root: Path, install_dir: Path, user_data: Path, *,
              bios: Path | None = None, cue: Path | None = None,
              selector: Callable[[str, str], Path | None] = select_file,
              orchestrator: Callable[..., dict] = orchestrate_first_run,
              launch: bool = True) -> dict:
    bios = bios or selector("Select your lawful PlayStation BIOS", "BIOS files\0*.bin\0\0")
    if bios is None: raise SetupCancelled("BIOS selection cancelled")
    cue = cue or selector("Select your WipEout 3 Special Edition CUE", "CUE sheets\0*.cue\0\0")
    if cue is None: raise SetupCancelled("disc selection cancelled")
    payload = package_root / "setup-payload"
    contract = json.loads((payload / "setup-contract.json").read_text(encoding="utf-8"))
    active_path = user_data / "receipts" / "active-setup.json"
    old_active = active_path.read_bytes() if active_path.exists() else None
    try:
        active = orchestrator(cue=cue, bios=bios, patch=payload / contract["patch"],
            xdelta3=payload / contract["xdelta3"], compiler=payload / contract["compiler"],
            regions=payload / contract["regions"], artifacts=payload / contract["artifacts"],
            artifact_root=payload / contract["artifact_root"], user_data=user_data,
            profile=payload / contract["profile"], media_manifest=payload / contract["media_manifest"],
            game_shards=payload / contract["game_shards"],
            game_shard_manifest_root=payload / contract["game_shard_manifest_root"])
        runtime_audit = install_runtime(package_root / "runtime-payload", install_dir)
    except Exception:
        if old_active is not None:
            active_path.parent.mkdir(parents=True, exist_ok=True); active_path.write_bytes(old_active)
        elif active_path.exists(): active_path.unlink()
        raise
    if launch:
        subprocess.Popen([str(install_dir / "bin" / "Wipeout3SE.exe"),
                          "--user-data", str(user_data)], cwd=install_dir)
    return {"status": "installed-not-game-ready", "active_setup": active,
            "runtime_audit": runtime_audit, "game_ready": False}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bios", type=Path); parser.add_argument("--cue", type=Path)
    parser.add_argument("--install-dir", type=Path)
    parser.add_argument("--user-data", type=Path)
    parser.add_argument("--no-launch", action="store_true")
    args = parser.parse_args(argv)
    root = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parents[2]
    local = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    try:
        receipt = run_setup(root, args.install_dir or local / "Programs" / "Wipeout3SE",
                            args.user_data or local / "Wipeout3SE", bios=args.bios, cue=args.cue,
                            launch=not args.no_launch)
        print(json.dumps(receipt, ensure_ascii=False, sort_keys=True)); return 0
    except SetupCancelled:
        return 1
    except Exception as error:
        ctypes.windll.user32.MessageBoxW(None, str(error), "Wipeout3SE Setup failed", 0x10)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
