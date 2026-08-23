#!/usr/bin/env python3
"""Run one isolated PSX_FPS_TELEMETRY benchmark and write a durable receipt.

The runner launches exactly one child without a shell. It refuses to start
when another process has the same image name, gives the child a new writable
card directory, and only ever terminates the ``Popen`` object it created.
Receipts distinguish measured binary/runtime identity from nearby source-tree
identity; proximity is not represented as proof that a binary matches source.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time
from typing import Iterable

import summarize_fps_telemetry as telemetry


RUNNER_SCHEMA = "psx-fps-benchmark-v2"
DEFAULT_COMPLETION_GRACE_SECONDS = 0.25
DEFAULT_MAX_HOST_BUSY_CORES = 2.5
MINIMUM_DURATION_SECONDS = 3.0
RESERVED_EXTRA_FLAGS = (
    "--bios",
    "--debug-port",
    "--disc",
    "--headless",
    "--launcher",
    "--memcard-dir",
    "--no-headless",
    "--no-launcher",
    "--renderer",
)
CODEGEN_TAG_RE = re.compile(
    r"overlay cache audit: tag=(?P<tag>[^\s]+)\s+"
    r"indexed=(?P<indexed>\d+)\s+candidates=(?P<candidates>\d+)\s+"
    r"status=(?P<status>[^\s]+)"
)
CACHE_NAMESPACE_RE = re.compile(r"cg\d+_[0-9a-fA-F]+_gc[0-9a-fA-F]+_f\d+")
CUE_FILE_RE = re.compile(r'^\s*FILE\s+(?:"([^"]+)"|(\S+))', re.IGNORECASE)


class BenchmarkError(RuntimeError):
    """A benchmark precondition or controlled child execution failed."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _file_identity(path: Path) -> dict[str, object]:
    stat = path.stat()
    return {
        "bytes": stat.st_size,
        "modified_unix_ns": stat.st_mtime_ns,
        "path": str(path.resolve()),
        "sha256": _sha256(path),
    }


def _require_file(value: str, label: str) -> Path:
    path = Path(value).expanduser().resolve()
    if not path.is_file():
        raise BenchmarkError(f"{label} is not a file: {path}")
    return path


def parse_tasklist_csv(output: str) -> list[dict[str, object]]:
    """Parse ``tasklist /NH /FO CSV`` rows without locale-sensitive columns."""
    processes: list[dict[str, object]] = []
    for row in csv.reader(output.splitlines()):
        if len(row) < 2 or row[0].startswith("INFO:"):
            continue
        try:
            pid = int(row[1])
        except ValueError:
            continue
        processes.append({"image": row[0], "pid": pid})
    return processes


def _tasklist(filter_image: str | None = None) -> list[dict[str, object]]:
    command = ["tasklist", "/NH", "/FO", "CSV"]
    if filter_image is not None:
        command.extend(("/FI", f"IMAGENAME eq {filter_image}"))
    query = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=10,
    )
    if query.returncode != 0:
        raise BenchmarkError(
            "cannot inspect host processes: "
            f"{query.stderr.strip() or query.stdout.strip()}"
        )
    return parse_tasklist_csv(query.stdout)


def same_image_processes(image_name: str) -> tuple[list[dict[str, object]], str]:
    """Return same-image processes using a direct OS query, never a shell."""
    if os.name == "nt":
        return _tasklist(image_name), "tasklist-direct-no-shell-image-name"

    proc = Path("/proc")
    if proc.is_dir():
        found: list[dict[str, object]] = []
        for entry in proc.iterdir():
            if not entry.name.isdigit():
                continue
            try:
                image = (entry / "exe").resolve(strict=True).name
            except OSError:
                continue
            if image == image_name:
                found.append({"image": image, "pid": int(entry.name)})
        return sorted(found, key=lambda item: int(item["pid"])), "procfs-exe-no-shell"
    raise BenchmarkError("same-image process guard is unavailable on this host")


def _validate_extra_args(extra_args: Iterable[str]) -> list[str]:
    values = list(extra_args)
    for value in values:
        for reserved in RESERVED_EXTRA_FLAGS:
            if value == reserved or value.startswith(reserved + "="):
                raise BenchmarkError(
                    f"--extra-arg may not override controlled option {reserved}"
                )
    return values


def build_command(
    executable: Path,
    bios: Path,
    disc: Path,
    saves: Path,
    renderer: str,
    debug_port: int | None,
    extra_args: Iterable[str] = (),
) -> list[str]:
    extras = _validate_extra_args(extra_args)
    command = [
        str(executable),
        "--no-launcher",
        "--headless",
        "--renderer",
        renderer,
        "--bios",
        str(bios),
        "--disc",
        str(disc),
        "--memcard-dir",
        str(saves),
    ]
    if debug_port is not None:
        command.extend(("--debug-port", str(debug_port)))
    command.extend(extras)
    return command


def minimum_telemetry_windows(duration_seconds: float) -> int:
    """Require enough roughly one-second windows to represent the run."""
    return max(2, math.floor(duration_seconds * 0.80))


def duration_completed(
    elapsed_seconds: float,
    requested_seconds: float,
    grace_seconds: float,
) -> bool:
    return elapsed_seconds + grace_seconds >= requested_seconds


def _windows_memory() -> dict[str, int]:
    class MemoryStatus(ctypes.Structure):
        _fields_ = [
            ("length", ctypes.c_ulong),
            ("memory_load_percent", ctypes.c_ulong),
            ("total_physical_bytes", ctypes.c_ulonglong),
            ("available_physical_bytes", ctypes.c_ulonglong),
            ("total_page_file_bytes", ctypes.c_ulonglong),
            ("available_page_file_bytes", ctypes.c_ulonglong),
            ("total_virtual_bytes", ctypes.c_ulonglong),
            ("available_virtual_bytes", ctypes.c_ulonglong),
            ("available_extended_virtual_bytes", ctypes.c_ulonglong),
        ]

    status = MemoryStatus()
    status.length = ctypes.sizeof(status)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        raise OSError("GlobalMemoryStatusEx failed")
    return {
        "available_physical_bytes": int(status.available_physical_bytes),
        "memory_load_percent": int(status.memory_load_percent),
        "total_physical_bytes": int(status.total_physical_bytes),
    }


def _filetime_value(value: object) -> int:
    return (int(value.dwHighDateTime) << 32) | int(value.dwLowDateTime)


def _windows_cpu_times() -> dict[str, int]:
    from ctypes import wintypes

    idle = wintypes.FILETIME()
    kernel = wintypes.FILETIME()
    user = wintypes.FILETIME()
    if not ctypes.windll.kernel32.GetSystemTimes(
        ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)
    ):
        raise OSError("GetSystemTimes failed")
    return {
        "idle_100ns": _filetime_value(idle),
        "kernel_100ns": _filetime_value(kernel),
        "user_100ns": _filetime_value(user),
    }


def _posix_memory() -> dict[str, int]:
    values: dict[str, int] = {}
    try:
        for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
            name, raw = line.split(":", 1)
            token = raw.strip().split()[0]
            values[name] = int(token) * 1024
    except (OSError, ValueError, IndexError):
        return {}
    return {
        "available_physical_bytes": values.get("MemAvailable", 0),
        "total_physical_bytes": values.get("MemTotal", 0),
    }


def _posix_cpu_times() -> dict[str, int]:
    try:
        first = Path("/proc/stat").read_text(encoding="ascii").splitlines()[0]
        parts = [int(value) for value in first.split()[1:]]
    except (OSError, ValueError, IndexError):
        return {}
    return {
        "idle_ticks": parts[3] + (parts[4] if len(parts) > 4 else 0),
        "total_ticks": sum(parts),
    }


def _cpu_model() -> str:
    if os.name == "nt":
        try:
            import winreg

            with winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                r"HARDWARE\DESCRIPTION\System\CentralProcessor\0",
            ) as key:
                return str(winreg.QueryValueEx(key, "ProcessorNameString")[0]).strip()
        except OSError:
            pass
    return platform.processor() or platform.machine()


def _process_count() -> tuple[int | None, str]:
    if os.name == "nt":
        try:
            return len(_tasklist()), "tasklist-direct-no-shell"
        except BenchmarkError:
            return None, "unavailable"
    proc = Path("/proc")
    if proc.is_dir():
        return sum(entry.name.isdigit() for entry in proc.iterdir()), "procfs-no-shell"
    return None, "unavailable"


def host_snapshot(
    image_name: str,
    same_image: list[dict[str, object]] | None = None,
) -> dict[str, object]:
    if same_image is None:
        same_image, guard = same_image_processes(image_name)
    else:
        guard = "provided-by-same-image-guard"
    count, count_source = _process_count()
    errors: list[str] = []
    try:
        memory = _windows_memory() if os.name == "nt" else _posix_memory()
    except OSError as exc:
        memory = {}
        errors.append(str(exc))
    try:
        cpu_times = _windows_cpu_times() if os.name == "nt" else _posix_cpu_times()
    except OSError as exc:
        cpu_times = {}
        errors.append(str(exc))
    return {
        "cpu": {
            "logical_processors": os.cpu_count(),
            "model": _cpu_model(),
            "system_times": cpu_times,
        },
        "errors": errors,
        "memory": memory,
        "platform": platform.platform(),
        "processes": {
            "count": count,
            "count_source": count_source,
            "same_image": same_image,
            "same_image_source": guard,
        },
        "unix_seconds": round(time.time(), 6),
    }


def host_delta(start: dict[str, object], end: dict[str, object]) -> dict[str, object]:
    start_times = start.get("cpu", {}).get("system_times", {})
    end_times = end.get("cpu", {}).get("system_times", {})
    busy_percent: float | None = None
    if "kernel_100ns" in start_times and "kernel_100ns" in end_times:
        idle = end_times["idle_100ns"] - start_times["idle_100ns"]
        total = (
            end_times["kernel_100ns"] - start_times["kernel_100ns"]
            + end_times["user_100ns"] - start_times["user_100ns"]
        )
        if total > 0:
            busy_percent = round(100.0 * (1.0 - idle / total), 6)
    elif "total_ticks" in start_times and "total_ticks" in end_times:
        idle = end_times["idle_ticks"] - start_times["idle_ticks"]
        total = end_times["total_ticks"] - start_times["total_ticks"]
        if total > 0:
            busy_percent = round(100.0 * (1.0 - idle / total), 6)
    logical_processors = start.get("cpu", {}).get("logical_processors")
    busy_cores: float | None = None
    if busy_percent is not None and isinstance(logical_processors, int):
        busy_cores = round(busy_percent * logical_processors / 100.0, 6)
    return {
        "system_cpu_busy_cores": busy_cores,
        "system_cpu_busy_percent": busy_percent,
    }


def host_load_rejection(
    delta: dict[str, object], maximum_busy_cores: float
) -> str | None:
    """Reject measurements whose total host load can mask single-thread speed."""
    busy_cores = delta.get("system_cpu_busy_cores")
    if not isinstance(busy_cores, (int, float)):
        return "host CPU load unavailable; cannot prove an uncontaminated run"
    if busy_cores > maximum_busy_cores:
        return (
            f"host CPU load averaged {busy_cores:.3f} logical cores; "
            f"maximum accepted is {maximum_busy_cores:.3f}"
        )
    return None


def _git_capture(root: Path, arguments: list[str]) -> str | None:
    try:
        result = subprocess.run(
            ["git", "-C", str(root), *arguments],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def _find_repository(path: Path) -> Path | None:
    for candidate in (path, *path.parents):
        if (candidate / ".git").exists():
            return candidate
    return None


def source_identity(executable: Path) -> dict[str, object]:
    root = _find_repository(executable.parent)
    if root is None:
        return {"discovered": False}
    status = _git_capture(root, ["status", "--porcelain", "--untracked-files=no"])
    value: dict[str, object] = {
        "association_to_binary": "ancestor-proximity-only; binary match unverified",
        "branch": _git_capture(root, ["branch", "--show-current"]),
        "discovered": True,
        "head": _git_capture(root, ["rev-parse", "HEAD"]),
        "root": str(root.resolve()),
        "tracked_worktree_dirty": bool(status),
        "tracked_worktree_status_sha256": (
            hashlib.sha256(status.encode("utf-8")).hexdigest() if status else None
        ),
    }
    submodule = root / "psxrecomp"
    if (submodule / ".git").exists():
        sub_status = _git_capture(
            submodule, ["status", "--porcelain", "--untracked-files=no"]
        )
        value["psxrecomp"] = {
            "head": _git_capture(submodule, ["rev-parse", "HEAD"]),
            "tracked_worktree_dirty": bool(sub_status),
            "tracked_worktree_status_sha256": (
                hashlib.sha256(sub_status.encode("utf-8")).hexdigest()
                if sub_status else None
            ),
        }
        header = submodule / "runtime" / "include" / "overlay_codegen_hash.h"
        if header.is_file():
            text = header.read_text(encoding="utf-8", errors="replace")
            match = re.search(
                r"PSX_OVERLAY_CODEGEN_HASH\s+(?:0x)?([0-9a-fA-F]+)", text
            )
            value["current_source_codegen_header"] = {
                "association_to_binary": "unverified",
                "identity": _file_identity(header),
                "value_hex": match.group(1).lower() if match else None,
            }
    return value


def build_identity(executable: Path) -> dict[str, object]:
    directory = executable.parent
    value: dict[str, object] = {
        "binary": _file_identity(executable),
        "directory": str(directory),
        "metadata_files": {},
    }
    for name in ("CMakeCache.txt", "build.ninja", "game.toml", "psx_game_version.txt"):
        candidate = directory / name
        if candidate.is_file():
            value["metadata_files"][name] = _file_identity(candidate)
    cache_file = directory / "CMakeCache.txt"
    if cache_file.is_file():
        selected: dict[str, str] = {}
        wanted = {
            "CMAKE_BUILD_TYPE",
            "CMAKE_CXX_COMPILER",
            "CMAKE_C_COMPILER",
            "CMAKE_GENERATOR",
            "CMAKE_PROJECT_VERSION",
            "PSX_GAME_VERSION",
            "PSX_GAME_TIMING_RUNS",
        }
        for line in cache_file.read_text(encoding="utf-8", errors="replace").splitlines():
            if "=" not in line or ":" not in line.split("=", 1)[0]:
                continue
            lhs, raw = line.split("=", 1)
            name = lhs.split(":", 1)[0]
            if name in wanted:
                selected[name] = raw
        value["cmake_selected"] = selected
    return value


def disc_identity(disc: Path) -> dict[str, object]:
    value: dict[str, object] = {"descriptor": _file_identity(disc), "referenced": []}
    if disc.suffix.casefold() != ".cue":
        return value
    try:
        lines = disc.read_text(encoding="utf-8-sig", errors="strict").splitlines()
    except (OSError, UnicodeError):
        return value
    seen: set[Path] = set()
    for line in lines:
        match = CUE_FILE_RE.match(line)
        if not match:
            continue
        target = (disc.parent / (match.group(1) or match.group(2))).resolve()
        if target in seen:
            continue
        seen.add(target)
        if target.is_file():
            value["referenced"].append(_file_identity(target))
        else:
            value["referenced"].append({"missing": True, "path": str(target)})
    return value


def cache_identity(cache_root: Path) -> dict[str, object]:
    if not cache_root.is_dir():
        return {"exists": False, "path": str(cache_root.resolve())}
    manifest = hashlib.sha256()
    count = 0
    total_bytes = 0
    namespaces: set[str] = set()
    extensions: dict[str, int] = {}
    for directory in cache_root.rglob("*"):
        if directory.is_dir() and CACHE_NAMESPACE_RE.fullmatch(directory.name):
            namespaces.add(directory.name)
    for path in sorted(
        (item for item in cache_root.rglob("*") if item.is_file()),
        key=lambda item: item.relative_to(cache_root).as_posix().casefold(),
    ):
        relative = path.relative_to(cache_root).as_posix()
        identity = _file_identity(path)
        manifest.update(relative.encode("utf-8"))
        manifest.update(b"\0")
        manifest.update(str(identity["bytes"]).encode("ascii"))
        manifest.update(b"\0")
        manifest.update(str(identity["sha256"]).encode("ascii"))
        manifest.update(b"\n")
        count += 1
        total_bytes += int(identity["bytes"])
        suffix = path.suffix.casefold() or "<none>"
        extensions[suffix] = extensions.get(suffix, 0) + 1
    return {
        "bytes": total_bytes,
        "exists": True,
        "extensions": dict(sorted(extensions.items())),
        "files": count,
        "manifest_sha256": manifest.hexdigest(),
        "namespaces": sorted(namespaces),
        "path": str(cache_root.resolve()),
    }


def cache_unchanged(
    start: dict[str, object], end: dict[str, object]
) -> bool:
    """Require the exact precompiled cache manifest to survive the run."""
    return start == end


def runtime_identity_from_logs(*paths: Path) -> dict[str, object]:
    value: dict[str, object] = {"cache_audits": [], "clock_configurations": []}
    for path in paths:
        try:
            lines = path.read_text(encoding="utf-8-sig", errors="replace").splitlines()
        except OSError:
            continue
        for line in lines:
            match = CODEGEN_TAG_RE.search(line)
            if match:
                value["cache_audits"].append({
                    "candidates": int(match.group("candidates")),
                    "indexed": int(match.group("indexed")),
                    "source": str(path.resolve()),
                    "status": match.group("status"),
                    "tag": match.group("tag"),
                })
            if "[CLOCK] title_crtc=" in line:
                fields = dict(
                    re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)", line)
                )
                value["clock_configurations"].append(fields)
    return value


def _render_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def _owned_child_cleanup(child: subprocess.Popen[bytes]) -> tuple[str, int | None]:
    """Stop only the one child object handed to us; never search by name/PID."""
    current = child.poll()
    if current is not None:
        return "already-exited", current
    child.terminate()
    try:
        return "terminated-owned-child", child.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        child.kill()
        return "killed-owned-child-after-terminate-timeout", child.wait(timeout=5.0)


def run(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--bios", required=True)
    parser.add_argument("--disc", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--duration", type=float, default=45.0)
    parser.add_argument(
        "--completion-grace",
        type=float,
        default=DEFAULT_COMPLETION_GRACE_SECONDS,
        help="maximum explicit duration-completion tolerance in seconds",
    )
    parser.add_argument("--renderer", default="software")
    parser.add_argument("--debug-port", type=int)
    parser.add_argument("--run-label", default="fps")
    parser.add_argument("--target-fps", type=float, default=telemetry.DEFAULT_TARGET_FPS)
    parser.add_argument(
        "--minimum-telemetry-windows",
        type=int,
        help="raise (but never lower) the duration-derived minimum",
    )
    parser.add_argument(
        "--max-host-busy-cores",
        type=float,
        default=DEFAULT_MAX_HOST_BUSY_CORES,
        help=(
            "maximum average total logical-core load, including the benchmark "
            "child, for an accepted run"
        ),
    )
    parser.add_argument("--extra-arg", action="append", default=[])
    args = parser.parse_args(argv)

    try:
        if not MINIMUM_DURATION_SECONDS <= args.duration <= 3600.0:
            raise BenchmarkError(
                f"--duration must be in [{MINIMUM_DURATION_SECONDS:g}, 3600] seconds"
            )
        if not 0.0 <= args.completion_grace <= min(2.0, args.duration * 0.10):
            raise BenchmarkError(
                "--completion-grace must be non-negative and no greater than "
                "2 seconds or 10% of duration"
            )
        if args.debug_port is not None and not 0 <= args.debug_port <= 65535:
            raise BenchmarkError("--debug-port must be in [0, 65535]")
        logical_processors = os.cpu_count()
        if (
            not math.isfinite(args.max_host_busy_cores)
            or args.max_host_busy_cores <= 0.0
            or (
                logical_processors is not None
                and args.max_host_busy_cores > logical_processors
            )
        ):
            raise BenchmarkError(
                "--max-host-busy-cores must be finite, positive, and no greater "
                "than the host logical-processor count"
            )
        executable = _require_file(args.exe, "executable")
        bios = _require_file(args.bios, "BIOS")
        disc = _require_file(args.disc, "disc image")
        extras = _validate_extra_args(args.extra_arg)

        preflight_existing, preflight_guard = same_image_processes(executable.name)
        if preflight_existing:
            detail = ", ".join(str(item["pid"]) for item in preflight_existing)
            raise BenchmarkError(
                f"refusing contaminated run: {executable.name} already has PID(s) {detail}"
            )

        derived_minimum = minimum_telemetry_windows(args.duration)
        if args.minimum_telemetry_windows is not None:
            if args.minimum_telemetry_windows < 2:
                raise BenchmarkError("--minimum-telemetry-windows must be at least 2")
            required_samples = max(derived_minimum, args.minimum_telemetry_windows)
        else:
            required_samples = derived_minimum

        output_dir = args.output_dir.expanduser().resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
        nonce = f"{time.time_ns() % 1_000_000_000:09d}"
        stem = f"{stamp}-{nonce}-{args.run_label}"
        stdout_path = output_dir / f"{stem}.stdout.log"
        stderr_path = output_dir / f"{stem}.stderr.log"
        receipt_path = output_dir / f"{stem}.receipt.json"
        run_path = output_dir / f"{stem}.run.json"
        saves = output_dir / f"{stem}-saves"
        for path in (stdout_path, stderr_path, receipt_path, run_path, saves):
            if path.exists():
                raise BenchmarkError(f"refusing to overwrite benchmark artifact: {path}")
        saves.mkdir()

        command = build_command(
            executable, bios, disc, saves, args.renderer, args.debug_port, extras
        )
        environment = os.environ.copy()
        scrubbed_names = sorted(name for name in environment if name.startswith("PSX_"))
        for name in scrubbed_names:
            environment.pop(name, None)
        environment["PSX_FPS_TELEMETRY"] = "1"
        creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0

        cache_root = executable.parent / "cache"
        start_cache = cache_identity(cache_root)
        build = build_identity(executable)
        source = source_identity(executable)
        inputs = {"bios": _file_identity(bios), "disc": disc_identity(disc)}
        runner_identity = _file_identity(Path(__file__).resolve())

        # Provenance hashing can be non-trivial for a CUE/BIN pair. Re-run the
        # contamination guard immediately before launch so a process started
        # during that work cannot slip into the measurement.
        existing, guard = same_image_processes(executable.name)
        if existing:
            detail = ", ".join(str(item["pid"]) for item in existing)
            raise BenchmarkError(
                f"refusing contaminated run at launch: {executable.name} "
                f"has PID(s) {detail}"
            )
        start_host = host_snapshot(executable.name, existing)

        overall_started_wall = time.time()
        child: subprocess.Popen[bytes] | None = None
        termination = "not-started"
        exit_code: int | None = None
        child_pid: int | None = None
        child_started_wall: float | None = None
        child_elapsed = 0.0
        try:
            with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
                child = subprocess.Popen(
                    command,
                    cwd=executable.parent,
                    env=environment,
                    stdin=subprocess.DEVNULL,
                    stdout=stdout,
                    stderr=stderr,
                    creationflags=creationflags,
                    shell=False,
                )
                child_pid = child.pid
                child_started_wall = time.time()
                child_started_mono = time.perf_counter()
                try:
                    exit_code = child.wait(timeout=args.duration)
                    termination = "natural"
                except subprocess.TimeoutExpired:
                    termination, exit_code = _owned_child_cleanup(child)
                finally:
                    if child.poll() is None:
                        termination, exit_code = _owned_child_cleanup(child)
                    child_elapsed = time.perf_counter() - child_started_mono
        except BaseException:
            if child is not None and child.poll() is None:
                _owned_child_cleanup(child)
            raise

        ended_wall = time.time()
        end_existing, end_guard = same_image_processes(executable.name)
        end_host = host_snapshot(executable.name, end_existing)
        measured_host_delta = host_delta(start_host, end_host)
        end_cache = cache_identity(cache_root)
        cache_was_unchanged = cache_unchanged(start_cache, end_cache)

        rejection_reasons: list[str] = []
        completed = duration_completed(
            child_elapsed, args.duration, args.completion_grace
        )
        if not completed:
            rejection_reasons.append(
                "child exited before requested duration outside explicit grace"
            )
        if termination == "natural" and exit_code not in (0, None):
            rejection_reasons.append(f"child exited naturally with code {exit_code}")
        if end_existing:
            rejection_reasons.append(
                "same-image process present at end: "
                + ", ".join(str(item["pid"]) for item in end_existing)
            )
        if not cache_was_unchanged:
            rejection_reasons.append(
                "overlay cache changed during measurement; precompile the exact "
                "cache and rerun"
            )
        host_rejection = host_load_rejection(
            measured_host_delta, args.max_host_busy_cores
        )
        if host_rejection is not None:
            rejection_reasons.append(host_rejection)

        run_receipt: dict[str, object] = {
            "artifacts": {
                "fresh_memcard_dir": str(saves),
                "stderr": str(stderr_path),
                "stdout": str(stdout_path),
            },
            "build_identity": build,
            "cache_identity": {
                "end": end_cache,
                "start": start_cache,
                "unchanged": cache_was_unchanged,
            },
            "child": {
                "elapsed_seconds": round(child_elapsed, 6),
                "end_unix_seconds": round(ended_wall, 6),
                "exit_code": exit_code,
                "pid": child_pid,
                "start_unix_seconds": (
                    round(child_started_wall, 6) if child_started_wall is not None else None
                ),
                "termination": termination,
            },
            "command": command,
            "duration": {
                "completed": completed,
                "completion_grace_seconds": args.completion_grace,
                "requested_seconds": args.duration,
            },
            "environment": {
                "inherited_variable_count": len(environment) - 1,
                "overrides": {"PSX_FPS_TELEMETRY": "1"},
                "scrubbed_names": scrubbed_names,
            },
            "features": {
                "fresh_memcard_directory": True,
                "headless": command.count("--headless") == 1,
                "no_launcher": command.count("--no-launcher") == 1,
                "owned_children_started": 1,
                "renderer": args.renderer,
                "shell": False,
                "stdin": "DEVNULL",
                "telemetry_opt_in": True,
            },
            "host": {
                "acceptance": {
                    "maximum_system_cpu_busy_cores": args.max_host_busy_cores,
                },
                "delta": measured_host_delta,
                "end": end_host,
                "start": start_host,
            },
            "inputs": inputs,
            "process_guard": {
                "end_source": end_guard,
                "existing_at_end": end_existing,
                "existing_at_start": existing,
                "existing_at_preflight": preflight_existing,
                "preflight_source": preflight_guard,
                "start_source": guard,
            },
            "python": {
                "executable": sys.executable,
                "implementation": platform.python_implementation(),
                "version": platform.python_version(),
            },
            "run_label": args.run_label,
            "runner_identity": runner_identity,
            "schema": RUNNER_SCHEMA,
            "source_identity": source,
            "start_unix_seconds": round(overall_started_wall, 6),
        }

        try:
            samples, log_source = telemetry.read_source(stderr_path)
            telemetry_receipt = telemetry.build_receipt(
                samples, [log_source], args.target_fps
            )
            if len(samples) < required_samples:
                rejection_reasons.append(
                    f"only {len(samples)} telemetry windows; require {required_samples} "
                    f"for {args.duration:g}s"
                )
            if int(log_source["configuration_records"]) < 1:
                rejection_reasons.append("missing startup clock configuration record")
        except telemetry.TelemetryError as exc:
            samples = []
            telemetry_receipt = {"error": str(exc)}
            rejection_reasons.append(f"telemetry invalid: {exc}")

        run_receipt["runtime_identity"] = runtime_identity_from_logs(
            stdout_path, stderr_path
        )
        run_receipt["telemetry_acceptance"] = {
            "duration_derived_minimum_windows": derived_minimum,
            "required_windows": required_samples,
            "windows_observed": len(samples),
        }
        run_receipt["accepted"] = not rejection_reasons
        run_receipt["rejection_reasons"] = rejection_reasons
        receipt = {
            "accepted": not rejection_reasons,
            "rejection_reasons": rejection_reasons,
            "run": run_receipt,
            "schema": RUNNER_SCHEMA,
            "telemetry": telemetry_receipt,
        }
        _render_json(receipt_path, receipt)
        _render_json(run_path, run_receipt)
        print(receipt_path)
        if rejection_reasons:
            for reason in rejection_reasons:
                print(f"rejected: {reason}", file=sys.stderr)
            return 2
        return 0
    except (
        BenchmarkError,
        OSError,
        subprocess.SubprocessError,
        telemetry.TelemetryError,
    ) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


def main() -> int:
    return run(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
