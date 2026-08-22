#!/usr/bin/env python3
"""Advance the authenticated BIOS shell probe one exact live frontier at a time."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
import subprocess
from pathlib import Path

from tools.analysis.live_shell_frontier_batch import (
    authenticated_mandatory_ranges, authenticated_straight_line_span)


TERMINAL = re.compile(r"^(\d+) (\d+) (\d+) (\d+) (?:\d+ ){8}(\d+) (\d+) ", re.M)
CAPACITY_ASSIGNMENT = re.compile(r"c\.authenticated_region_capacity=\d+U;")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run(command: list[str], root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)


def synchronize_probe_capacity(probe: Path, binds: Path) -> int:
    """Size storage to the exact number of binding operations in the probe."""
    source = probe.read_text(encoding="utf-8")
    lambda_calls = len(re.findall(r"\bbind\(clean_aot_binding_", source))
    explicit_calls = len(re.findall(r"bind_authenticated_copied_epoch", source))
    static_operations = lambda_calls + max(0, explicit_calls - 1)
    generated_operations = sum(
        1 for line in binds.read_text(encoding="utf-8").splitlines()
        if line.strip())
    capacity = static_operations + generated_operations
    if capacity <= 0:
        raise RuntimeError("probe binding manifest is empty")
    assignment = f"c.authenticated_region_capacity={capacity}U;"
    if CAPACITY_ASSIGNMENT.search(source):
        updated = CAPACITY_ASSIGNMENT.sub(assignment, source, count=1)
    else:
        marker = "Runtime r(c);"
        if marker not in source:
            raise RuntimeError("probe runtime construction marker missing")
        updated = source.replace(marker, assignment + marker, 1)
    if updated != source:
        probe.write_text(updated, encoding="utf-8", newline="\n")
    return capacity


def instruction_span(image: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(image):
        raise RuntimeError("frontier lies outside authenticated image")
    word = int.from_bytes(image[offset:offset + 4], "little")
    opcode, function = word >> 26, word & 0x3f
    control = opcode in {1, 2, 3, 4, 5, 6, 7} or (opcode == 0 and function in {8, 9})
    return 8 if control else 4


def probe(root: Path) -> tuple[int, int, int, int, int, str]:
    result = run([str(root / "build-bios-frontier-c0" / "bios_frontier_c0.exe")], root)
    if result.returncode:
        raise RuntimeError(f"probe failed ({result.returncode})\n{result.stdout[-4000:]}")
    matches = list(TERMINAL.finditer(result.stdout))
    if not matches:
        raise RuntimeError(f"probe terminal line missing\n{result.stdout[-4000:]}")
    match = matches[-1]
    instructions, dispatches, status, pc, cd_reads, cd_writes = map(int, match.groups())
    return instructions, dispatches, status, pc, cd_reads, cd_writes, result.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--first-index", type=int, default=269)
    parser.add_argument("--max-batch-instructions", type=int, default=1,
                        help="batch authenticated straight-line instructions before one rebuild")
    parser.add_argument("--max-direct-closure-instructions", type=int, default=0,
                        help="also follow authenticated mandatory plain-J corridors")
    parser.add_argument("--normalize-receipt", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    receipt = root / "build-bios-frontier-c0" / "live-frontiers.receipt"
    if args.normalize_receipt:
        receipt_text = receipt.read_text(encoding="utf-8")
        parsed = json.loads(receipt_text) if receipt_text.lstrip().startswith("{") else None
        rows = parsed["frontiers"] if parsed is not None else [
            ast.literal_eval(line) for line in receipt_text.splitlines() if line]
        if rows and rows[0].get("pc") == "0x80050A88":
            rows[0:0] = [
                {"ordinal": -3, "pc": "0x80050A7C", "instructions": 19271351,
                 "dispatches": 1787466, "status": 2, "cd_reads": 0, "cd_writes": 0, "sectors": 0},
                {"ordinal": -2, "pc": "0x80050A80", "instructions": 19271352,
                 "dispatches": 1787467, "status": 2, "cd_reads": 0, "cd_writes": 0, "sectors": 0},
                {"ordinal": -1, "pc": "0x80050A84", "instructions": 19271353,
                 "dispatches": 1787468, "status": 2, "cd_reads": 0, "cd_writes": 0, "sectors": 0},
            ]
        bind_text = (root / "build-bios-frontier-c0-src" / "auto_frontier_binds.inc").read_text(encoding="utf-8")
        source_hashes = {int(index): source_hash for source_hash, index in re.findall(
            r'AuthenticatedCopiedEpochBinding e\{[^\n]*?,"([0-9a-f]{64})",x->executable_sha256[^\n]*?"LIVE(\d+) "',
            bind_text)}
        bindings = []
        for path in sorted(root.glob("build-shell-exact-live-*.manifest")):
            index_match = re.search(r"live-(\d+)\.manifest$", path.name)
            if index_match is None:
                continue
            index = int(index_match.group(1))
            manifest_lines = [line.split("=", 1) for line in path.read_text(encoding="utf-8").splitlines()]
            fields = dict(manifest_lines)
            codes = [value for key, value in manifest_lines if key == "code"]
            bindings.append({"index": index, "source_sha256": source_hashes[index],
                             "variant_sha256": fields["variant_sha256"],
                             "image_sha256": fields["image_sha256"],
                             "load_start": fields["load_start"],
                             "load_end_exclusive": fields["load_end_exclusive"],
                             "manifest_sha256": sha(path.read_bytes()),
                             "code_ranges": codes})
        document = {"schema": "clean-aot-live-shell-frontiers-v1",
                    "route_manifest_commit": "46da91b", "dispatch_bound": 3000000,
                    "frontier_count": max(0, len(rows) - 1), "frontiers": rows,
                    "bindings": bindings}
        receipt.write_text(json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n",
                           encoding="utf-8", newline="\n")
        return 0
    compiler = root / "build-clean-aot" / "compiler" / "clean_aot_compiler_cli.exe"
    decls = root / "build-bios-frontier-c0-src" / "auto_frontier_decls.inc"
    binds = root / "build-bios-frontier-c0-src" / "auto_frontier_binds.inc"
    bios_path = root / "build-oracle" / "goosestation" / "system" / "SCPH1001.BIN"
    probe_source = root / "build-bios-frontier-c0-src" / "probe.cpp"
    seen: set[tuple[int, str]] = set()

    synchronize_probe_capacity(probe_source, binds)
    build = run(["cmake", "--build", "build-bios-frontier-c0", "--target",
                 "bios_frontier_c0", "-j", "8"], root)
    if build.returncode:
        raise RuntimeError(build.stdout[-8000:])
    history: list[dict[str, int | str]] = []
    for ordinal in range(args.count + 1):
        instructions, dispatches, status, pc, cd_reads, cd_writes, output = probe(root)
        sectors = 0
        game = re.findall(r"^GAME .* (\d+)$", output, re.M)
        if game:
            sectors = int(game[-1])
        row = {"ordinal": ordinal, "pc": f"0x{pc:08X}", "instructions": instructions,
               "dispatches": dispatches, "status": status, "cd_reads": cd_reads,
               "cd_writes": cd_writes, "sectors": sectors}
        history.append(row)
        print("FRONTIER", row, flush=True)
        if pc == 0x80030120 or cd_reads or cd_writes or sectors or ordinal == args.count:
            break

        ram = (root / "build-bios-frontier-c0" / "ram-full.bin").read_bytes()
        physical = pc & 0x1fffffff
        if 0x1fc00000 <= physical < 0x1fc80000:
            source, source_start, source_count = bios_path.read_bytes(), 0xbfc00000, 0x80000
            image_base = 0xbfc00000
            offset = physical - 0x1fc00000
        elif physical < len(ram):
            source, source_start, source_count = ram, 0, len(ram)
            # The authenticated copied-epoch source window is physical RAM,
            # while live CPU frontiers retain their KSEG virtual address.
            image_base = pc - physical
            offset = physical
        else:
            print("MATERIAL_BLOCKER frontier-outside-ram-bios", flush=True)
            break
        source_hash = sha(source)
        key = (pc, source_hash)
        if key in seen:
            print("MATERIAL_BLOCKER repeated-exact-frontier", flush=True)
            break
        seen.add(key)
        if args.max_direct_closure_instructions:
            ranges = authenticated_mandatory_ranges(
                source, image_base, pc, args.max_direct_closure_instructions)
        else:
            span = authenticated_straight_line_span(source, offset, args.max_batch_instructions)
            ranges = [(pc, pc + span)]
        payload_start, payload_end = ranges[0][0], ranges[-1][1]
        payload_offset = payload_start - image_base
        payload = source[payload_offset:payload_offset + payload_end - payload_start]
        range_contract = ",".join(f"{start:08x}-{end:08x}" for start, end in ranges)
        identity_input = (f"{source_hash}:{ranges[0][0]:08x}:{ranges[0][1]:08x}"
                          if len(ranges) == 1 else f"{source_hash}:ranges:{range_contract}")
        identity = sha(identity_input.encode("ascii"))
        image_hash = sha(payload)
        index = args.first_index + ordinal
        stem = f"build-shell-exact-live-{index:03d}"
        image_path = root / f"{stem}.bin"
        manifest_path = root / f"{stem}.manifest"
        instruction_count = sum((end - start) // 4 for start, end in ranges)
        output_suffix = f"{instruction_count}i" if args.max_direct_closure_instructions else str(span)
        output_path = root / f"{stem}-{output_suffix}"
        image_path.write_bytes(payload)
        manifest_path.write_text(
            "schema=clean-aot-compiler-manifest-v1\n"
            "image_kind=raw-payload\n"
            "symbol_namespace=variant\n"
            f"variant_sha256={identity}\nimage_sha256={image_hash}\n"
            f"load_start=0x{payload_start:08x}\nload_end_exclusive=0x{payload_end:08x}\n"
            + "".join(f"entry=0x{start:08x}\n" for start, _end in ranges)
            + "".join(f"code=0x{start:08x}-0x{end:08x}\n" for start, end in ranges),
            encoding="utf-8", newline="\n")
        compiled = run([str(compiler), "--image", str(image_path), "--manifest",
                        str(manifest_path), "--output", str(output_path)], root)
        if compiled.returncode:
            raise RuntimeError(f"compiler rejected frontier {pc:#x}\n{compiled.stdout}")
        with decls.open("a", encoding="utf-8", newline="\n") as stream:
            stream.write(f'extern "C" const GeneratedExecutableBinding* clean_aot_binding_{identity}() noexcept;\n')
        with binds.open("a", encoding="utf-8", newline="\n") as stream:
            stream.write(
                f'{{const auto*x=clean_aot_binding_{identity}();Runtime::AuthenticatedCopiedEpochBinding e'
                f'{{x->variant_identity_sha256,0x{source_start:x}U,0x{source_count:x}U,"{source_hash}",'
                f'x->executable_sha256,*x}};s=r.bind_authenticated_copied_epoch(e);std::cerr<<"LIVE{index} "'
                f'<<static_cast<unsigned>(s)<<\'\\n\';run();}}\n')
        synchronize_probe_capacity(probe_source, binds)
        build = run(["cmake", "--build", "build-bios-frontier-c0", "--target",
                     "bios_frontier_c0", "-j", "8"], root)
        if build.returncode:
            raise RuntimeError(f"build failed at frontier {pc:#x}\n{build.stdout[-8000:]}")

    document = {"schema": "clean-aot-live-shell-frontiers-v1",
                "route_manifest_commit": "46da91b", "dispatch_bound": 3000000,
                "frontier_count": max(0, len(history) - 1), "frontiers": history}
    receipt.write_text(json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n",
                       encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
