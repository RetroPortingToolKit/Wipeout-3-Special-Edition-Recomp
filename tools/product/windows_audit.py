"""Windows installed-package dependency, link-map, and frontier audit."""

from __future__ import annotations

import hashlib
import json
import struct
import subprocess
from pathlib import Path
from typing import Any

from tools.cleanroom.gate_common import GateError, scan_link_map
if __package__ in {None, ""}:
    from pipeline import canonical_json_bytes, load_json  # type: ignore[import-not-found]
    from release_lane import ReleaseError, validate_release  # type: ignore[import-not-found]
else:
    from .pipeline import canonical_json_bytes, load_json
    from .release_lane import ReleaseError, validate_release

SYSTEM_DLLS = {name.casefold() for name in (
    "kernel32.dll", "user32.dll", "advapi32.dll", "shell32.dll", "ole32.dll",
    "oleaut32.dll", "gdi32.dll", "comdlg32.dll", "combase.dll", "ntdll.dll",
    "secur32.dll", "setupapi.dll", "cfgmgr32.dll", "imm32.dll", "version.dll",
    "winmm.dll", "ws2_32.dll", "bcrypt.dll", "crypt32.dll", "shlwapi.dll",
    "windowscodecs.dll", "vulkan-1.dll", "dwmapi.dll", "uxtheme.dll", "msvcrt.dll")}
MINIMUM_AUTHENTICATED_BIOS_INSTRUCTIONS = 79_405
MINIMUM_INSTALLED_PRODUCT_BIOS_INSTRUCTIONS = 71_501
AUTHENTICATED_FRONTIER_SCHEMAS = {
    "clean-aot.real-bios-execution.v1",
    "clean-aot.real-bios-execution.v2",
}


def _hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pe_imports(path: Path) -> list[str]:
    data = path.read_bytes()
    if len(data) < 256 or data[:2] != b"MZ": raise ReleaseError("payload is not a PE image")
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ReleaseError("payload has an invalid PE signature")
    sections, optional_size = struct.unpack_from("<H12xH", data, pe + 6)
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    directory = optional + (112 if magic == 0x20b else 96 if magic == 0x10b else -1)
    if directory < optional or directory + 16 > optional + optional_size:
        raise ReleaseError("payload has an unsupported PE optional header")
    import_rva, import_size = struct.unpack_from("<II", data, directory + 8)
    section_table = optional + optional_size
    table = []
    for index in range(sections):
        offset = section_table + index * 40
        if offset + 40 > len(data): raise ReleaseError("payload has a truncated PE section table")
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        table.append((virtual_address, max(virtual_size, raw_size), raw_offset, raw_size))
    def locate(rva: int) -> int:
        for address, span, raw, raw_size in table:
            if address <= rva < address + span and rva - address < raw_size:
                result = raw + rva - address
                if result < len(data): return result
        raise ReleaseError("PE import RVA is outside file-backed sections")
    if import_rva == 0 or import_size == 0: return []
    cursor, end, result = locate(import_rva), locate(import_rva) + import_size, []
    while cursor + 20 <= len(data) and cursor < end:
        descriptor = struct.unpack_from("<IIIII", data, cursor)
        if descriptor == (0, 0, 0, 0, 0): break
        name = locate(descriptor[3]); terminator = data.find(b"\0", name, min(len(data), name + 260))
        if terminator < 0: raise ReleaseError("PE import name is unterminated")
        try: result.append(data[name:terminator].decode("ascii"))
        except UnicodeError as error: raise ReleaseError("PE import name is not ASCII") from error
        cursor += 20
    return sorted(set(result), key=str.casefold)


def audit_windows_package(package: Path, *, link_map: Path, root: Path,
                          frontier_evidence: Path, allowlist: Path | None = None) -> dict[str, Any]:
    package_receipt = validate_release(package)
    try: link_receipt = scan_link_map(link_map, root=root.resolve(strict=True), allowlist_path=allowlist)
    except GateError as error: raise ReleaseError(str(error)) from error
    if link_receipt["status"] != "pass": raise ReleaseError("package link-map leakage detected")
    frontier = load_json(frontier_evidence)
    if (frontier.get("schema") not in AUTHENTICATED_FRONTIER_SCHEMAS
            or frontier.get("bios_instructions_executed", 0) < MINIMUM_AUTHENTICATED_BIOS_INSTRUCTIONS):
        raise ReleaseError("BIOS frontier evidence is stale")
    binaries = [path for path in package.rglob("*")
                if path.is_file() and path.suffix.casefold() in {".exe", ".dll"}]
    packaged = {path.name.casefold() for path in binaries}
    imports: dict[str, list[str]] = {}
    missing: set[str] = set()
    for binary in binaries:
        names = pe_imports(binary)
        imports[binary.relative_to(package).as_posix()] = names
        for name in names:
            folded = name.casefold()
            if (folded.startswith("api-ms-win-") or folded.startswith("ext-ms-win-")
                    or folded in SYSTEM_DLLS or folded in packaged): continue
            missing.add(name)
    if missing: raise ReleaseError(f"package dependency closure is incomplete: {sorted(missing)}")
    return {"schema": "wipeout3se.clean-aot.windows-package-audit.v1",
            "status": "validated-not-game-ready", "game_ready": False,
            "package_manifest_sha256": package_receipt["manifest_sha256"],
            "link_map_receipt_sha256": hashlib.sha256(canonical_json_bytes(link_receipt)).hexdigest(),
            "frontier_evidence_sha256": _hash(frontier_evidence),
            "bios_instructions_executed": frontier["bios_instructions_executed"],
            "imports": imports}


def smoke_windows_product(package: Path, *, bios: Path, receipt_path: Path,
                          frontier_evidence: Path,
                          minimum_bios_instructions: int = MINIMUM_INSTALLED_PRODUCT_BIOS_INSTRUCTIONS) -> dict[str, Any]:
    package_receipt = validate_release(package)
    package = package.resolve(strict=True)
    bios = bios.resolve(strict=True)
    authenticated = load_json(frontier_evidence)
    if (authenticated.get("schema") not in AUTHENTICATED_FRONTIER_SCHEMAS
            or type(authenticated.get("bios_instructions_executed")) is not int
            or authenticated["bios_instructions_executed"] < minimum_bios_instructions
            or authenticated.get("bios_sha256") != _hash(bios)):
        raise ReleaseError("authenticated smoke frontier is stale or belongs to another BIOS")
    try: bios.relative_to(package)
    except ValueError: pass
    else: raise ReleaseError("copyrighted BIOS input must remain outside the package")
    completed = subprocess.run(
        [str(package / "bin" / "Wipeout3SE.exe"), "--bios", str(bios), "--smoke-hidden"],
        cwd=package / "bin", stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=30.0, check=False)
    try: frontier = json.loads(completed.stdout.decode("utf-8").strip().splitlines()[-1])
    except (UnicodeError, ValueError, IndexError) as error:
        raise ReleaseError("installed product emitted no boot frontier") from error
    if (completed.returncode != 0 or frontier.get("schema") != "clean-aot.bootstrap-receipt.v1"
            or frontier.get("stage") != "bios-slice-stopped"
            or frontier.get("reason") != "unknown-generated-bios-pc"
            or type(frontier.get("bios_instructions_executed")) is not int
            or frontier["bios_instructions_executed"] < minimum_bios_instructions
            or frontier["bios_instructions_executed"] > authenticated["bios_instructions_executed"]):
        raise ReleaseError("installed product did not reach the required fail-closed frontier")
    receipt = {"schema": "wipeout3se.clean-aot.windows-installed-smoke.v1",
               "status": "passed-not-game-ready", "game_ready": False,
               "package_manifest_sha256": package_receipt["manifest_sha256"],
               "bios_input_sha256": _hash(bios), "frontier": frontier,
               "authenticated_frontier": {
                   "schema": authenticated["schema"],
                   "sha256": _hash(frontier_evidence),
                   "bios_instructions_executed": authenticated["bios_instructions_executed"]},
               "stdout_sha256": hashlib.sha256(completed.stdout).hexdigest(),
               "stderr_sha256": hashlib.sha256(completed.stderr).hexdigest()}
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    if receipt_path.exists(): raise ReleaseError("refusing to overwrite smoke receipt")
    receipt_path.write_bytes(canonical_json_bytes(receipt) + b"\n")
    return receipt
