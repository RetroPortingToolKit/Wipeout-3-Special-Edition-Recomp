#!/usr/bin/env python3
"""Generate the product epoch registry from authenticated receipts and setup artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

SCHEMA = "wipeout3se.clean-aot.epoch-registry.v1"
HEX = r"[0-9a-f]{64}"
BINDING = re.compile(r'GeneratedExecutableBinding\s+value\{"(' + HEX + r')","(' + HEX + r')"')
ACCESSOR = re.compile(r'GeneratedExecutableBinding\*\s+(clean_aot_binding_(' + HEX + r'))\(\)')


class RegistryError(RuntimeError): pass


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def generate(evidence_dir: Path, generated_root: Path, output: Path,
             game_shards: Path | None = None) -> dict:
    receipts = []
    paths = sorted(evidence_dir.glob("bios-frontier-*.json"))
    repository = next((parent for parent in [evidence_dir.resolve(), *evidence_dir.resolve().parents]
                       if (parent / ".git").exists()), None)
    if repository is not None:
        tracked = subprocess.run(["git", "ls-files", "--", evidence_dir.resolve().relative_to(repository).as_posix()],
                                 cwd=repository, check=True, capture_output=True, text=True).stdout.splitlines()
        tracked_paths = {(repository / value).resolve() for value in tracked}
        paths = [path for path in paths if path.resolve() in tracked_paths]
    for path in paths:
        value = json.loads(path.read_text(encoding="utf-8"))
        if value.get("schema") == "clean-aot.real-bios-execution.v2": receipts.append((path, value))
    if not receipts: raise RegistryError("no authenticated v2 frontier receipts")
    latest_path, latest = max(receipts, key=lambda item: item[1].get("bios_instructions_executed", -1))
    bios_identity = latest.get("bios_identity_sha256")
    required = []
    for _path, receipt in sorted(receipts, key=lambda item: item[1]["bios_instructions_executed"]):
        if receipt.get("bios_sha256") != latest.get("bios_sha256"):
            raise RegistryError("frontier receipt chain mixes BIOS identities")
        for transition in receipt.get("transitions", []):
            identity = transition.get("variant_identity_sha256")
            if identity and identity != bios_identity and identity not in required: required.append(identity)
    candidates: dict[str, list[dict]] = {}
    for table in sorted(generated_root.rglob("entry_table.cpp")):
        text = table.read_text(encoding="utf-8")
        binding, accessor = BINDING.search(text), ACCESSOR.search(text)
        if not binding or not accessor or binding.group(1) != accessor.group(2):
            raise RegistryError(f"malformed generated binding metadata: {table}")
        sources = sorted(table.parent.glob("*.cpp"))
        candidates.setdefault(binding.group(1), []).append({"accessor": accessor.group(1),
            "executable_sha256": binding.group(2), "directory": table.parent.resolve(), "sources": sources})
    game_required = []
    game_document = None
    if game_shards is not None:
        game_document = json.loads(game_shards.read_text(encoding="utf-8"))
        if (game_document.get("schema") != "wo3se.generated-game-shards.v1" or
                game_document.get("status") != "partial-fail-closed" or
                game_document.get("policy", {}).get("unknown_pc_action") != "reject"):
            raise RegistryError("game shard authority is not the authenticated fail-closed set")
        images = game_document.get("images")
        if not isinstance(images, list) or len(images) != 2:
            raise RegistryError("game shard authority must name exactly two images")
        if game_document.get("compiled_region_count") != sum(
                image.get("compiled_region_count", -1) for image in images):
            raise RegistryError("game shard region count is inconsistent")
        if game_document.get("compiled_instruction_count") != sum(
                image.get("compiled_instruction_count", -1) for image in images):
            raise RegistryError("game shard instruction count is inconsistent")
        for image in images:
            identity = image.get("identity_sha256")
            if not isinstance(identity, str) or not re.fullmatch(HEX, identity):
                raise RegistryError("game shard identity is malformed")
            game_required.append(identity)
    referenced = set(required) | set(game_required)
    unreferenced = sorted(set(candidates) - referenced - {bios_identity})
    if unreferenced: raise RegistryError(f"generated artifacts are absent from authenticated chain: {unreferenced}")
    # Superseded historical receipts can name identities no longer present in the
    # finalized setup catalog. The generated catalog is authoritative for which
    # currently authenticated epochs ship; every one must still be receipt-backed.
    required = [identity for identity in required if identity in candidates]
    latest_required = {item.get("variant_identity_sha256") for item in latest.get("transitions", [])
                       if item.get("variant_identity_sha256") != bios_identity}
    if not latest_required.issubset(candidates):
        raise RegistryError("latest authenticated frontier is missing generated artifacts")
    entries = []
    for identity in required:
        matches = candidates.get(identity, [])
        if len(matches) != 1: raise RegistryError(f"identity must resolve one-to-one: {identity} ({len(matches)})")
        item = matches[0]
        if item["accessor"] != f"clean_aot_binding_{identity}":
            raise RegistryError(f"identity/accessor mismatch: {identity}")
        entries.append({"identity_sha256": identity, "executable_sha256": item["executable_sha256"],
                        "accessor": item["accessor"], "directory": item["directory"].as_posix(),
                        "sources": [p.resolve().as_posix() for p in item["sources"]]})
    game_entries = []
    for identity in game_required:
        matches = candidates.get(identity, [])
        if len(matches) != 1: raise RegistryError(f"game identity must resolve one-to-one: {identity} ({len(matches)})")
        item = matches[0]
        if item["accessor"] != f"clean_aot_binding_{identity}" or item["executable_sha256"] != identity:
            raise RegistryError(f"game identity/accessor/executable mismatch: {identity}")
        authority_image = next(image for image in game_document["images"] if image["identity_sha256"] == identity)
        game_entries.append({"identity_sha256": identity, "executable_sha256": identity,
                             "name": authority_image["name"],
                             "compiled_instruction_count": authority_image["compiled_instruction_count"],
                             "compiled_region_count": authority_image["compiled_region_count"],
                             "accessor": item["accessor"], "directory": item["directory"].as_posix(),
                             "sources": [p.resolve().as_posix() for p in item["sources"]]})
    inventory = [{k: v for k, v in item.items() if k != "sources"} for item in entries]
    document = {"schema": SCHEMA, "status": "generated-not-game-ready", "game_ready": False,
                "frontier": {"schema": latest["schema"], "bios_instructions_executed": latest["bios_instructions_executed"],
                             "sha256": hashlib.sha256(latest_path.read_bytes()).hexdigest()},
                "bios_identity_sha256": bios_identity, "bios_sha256": latest["bios_sha256"],
                "entries": inventory,
                "game_shards": {"compiled_instruction_count": 0 if game_document is None else game_document["compiled_instruction_count"],
                    "compiled_region_count": 0 if game_document is None else game_document["compiled_region_count"],
                    "authority_sha256": "" if game_document is None else hashlib.sha256(game_shards.read_bytes()).hexdigest(),
                    "entries": [{k: v for k, v in item.items() if k != "sources"} for item in game_entries]}}
    document["registry_sha256"] = hashlib.sha256(_canonical(document)).hexdigest()
    output.mkdir(parents=True, exist_ok=True)
    header = ["#pragma once", "#include <array>", "#include <clean_aot/runtime/runtime.hpp>"]
    header += [f'extern "C" const clean_aot::runtime::GeneratedExecutableBinding* {e["accessor"]}() noexcept;' for e in entries + game_entries]
    header += [f'inline std::array<const clean_aot::runtime::GeneratedExecutableBinding*, {len(entries)}> clean_aot_product_epoch_bindings() noexcept {{ return {{']
    header += [f'{e["accessor"]}(),' for e in entries] + ["}; }"]
    header += [f'inline std::array<const clean_aot::runtime::GeneratedExecutableBinding*, {len(game_entries)}> clean_aot_product_game_bindings() noexcept {{ return {{']
    header += [f'{e["accessor"]}(),' for e in game_entries] + ["}; }"]
    (output / "product_epoch_bindings.hpp").write_text("\n".join(header) + "\n", encoding="utf-8")
    sources = [source for entry in entries + game_entries for source in entry["sources"]]
    (output / "epoch-registry.cmake").write_text("set(CLEAN_AOT_REGISTRY_SOURCES\n" +
        "".join(f'  "{source}"\n' for source in sources) + ")\n", encoding="utf-8")
    (output / "epoch-registry.json").write_bytes(_canonical(document))
    return document


def main() -> int:
    parser = argparse.ArgumentParser(); parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--generated-root", type=Path, required=True); parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--game-shards", type=Path)
    args = parser.parse_args(); print(json.dumps(generate(args.evidence_dir, args.generated_root, args.output, args.game_shards), sort_keys=True)); return 0


if __name__ == "__main__": raise SystemExit(main())
