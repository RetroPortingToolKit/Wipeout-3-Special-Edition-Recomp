#!/usr/bin/env python3
"""Build a fail-closed media-to-static-coverage audit for game content."""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path
from typing import Any

SCHEMA = "wo3se.content-static-coverage.v1"


class AuditError(RuntimeError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load(path: Path) -> tuple[dict[str, Any], str]:
    raw = path.read_bytes()
    try:
        value = json.loads(raw)
    except json.JSONDecodeError as error:
        raise AuditError(f"invalid JSON: {path}") from error
    if not isinstance(value, dict):
        raise AuditError(f"receipt is not an object: {path}")
    return value, sha256(raw)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AuditError(message)


def media_path(inventory_path: Path) -> str:
    name = inventory_path.name.removesuffix(".json")
    parts = name.split("__", 1)
    require(len(parts) == 2, "expanded inventory filename lacks media path")
    return f"/{parts[0]}/{parts[1]}"


def build_audit(iso: dict[str, Any], iso_digest: str,
                extraction: dict[str, Any], extraction_digest: str,
                expanded: list[tuple[str, dict[str, Any], str]],
                nfe: dict[str, Any], nfe_digest: str,
                authorization: dict[str, Any], authorization_digest: str,
                finite: dict[str, Any], finite_digest: str,
                epochs: list[tuple[dict[str, Any], str]]) -> dict[str, Any]:
    require(iso.get("status") == "ok" and iso.get("schema") == "psxrecomp.iso9660-inventory.v1",
            "ISO inventory is not verified")
    require(iso.get("file_count") == len(iso.get("files", [])),
            "ISO file count disagrees with inventory")
    excluded_audio = [row for row in iso["files"]
                      if row.get("hash_status") == "extent-outside-image"]
    require(all(row.get("logical_path", "").endswith(".WAV")
                for row in excluded_audio) and
            all(row.get("hash_status") in {"ok", "extent-outside-image"}
                for row in iso["files"]),
            "ISO inventory contains an unexplained unverified file")
    iso_files = {row["logical_path"]: row for row in iso["files"]}
    require(len(iso_files) == len(iso["files"]), "ISO logical paths are ambiguous")

    extracted = extraction.get("extraction", {})
    require(extraction.get("status") == "ok" and
            extracted.get("file_count") == len(extracted.get("files", [])),
            "PBP extraction receipt is incomplete")
    extracted_files = {row["logical_path"]: row for row in extracted["files"]}
    require(len(extracted_files) == len(extracted["files"]),
            "PBP extraction paths are ambiguous")
    require(set(extracted_files).issubset(iso_files), "extracted PBP is absent from ISO")
    for name, row in extracted_files.items():
        require(row["sha256"] == iso_files[name]["sha256"] and
                row["bytes"] == iso_files[name]["bytes"],
                f"PBP extraction identity mismatch: {name}")

    inventories = {name: (value, digest) for name, value, digest in expanded}
    require(len(inventories) == len(expanded) and set(inventories) == set(extracted_files),
            "expanded inventories do not exactly cover extracted PBP files")
    payloads: list[dict[str, Any]] = []
    for name in sorted(inventories):
        value, digest = inventories[name]
        container = value.get("container", {})
        require(value.get("status") == "ok" and
                value.get("expanded_verification", {}).get("status") == "exact",
                f"PBP expansion is not exact: {name}")
        require(container.get("sha256") == extracted_files[name]["sha256"] and
                container.get("bytes") == extracted_files[name]["bytes"],
                f"expanded PBP identity mismatch: {name}")
        entries = value.get("entries", [])
        require(value.get("entry_count") == len(entries) and
                [row.get("index") for row in entries] == list(range(len(entries))),
                f"PBP entries are incomplete or unordered: {name}")
        for row in entries:
            payloads.append({
                "classification": "unclassified-payload",
                "container": name,
                "container_sha256": container["sha256"],
                "encoding": row["encoding"],
                "expanded_bytes": row["expanded_bytes"],
                "expanded_sha256": row["expanded_sha256"],
                "index": row["index"],
                "inventory_sha256": digest,
                "static_mapping": None,
            })

    nfe_container = nfe.get("container", {})
    require(nfe.get("status") == "ok" and
            nfe.get("expanded_verification", {}).get("status") == "exact",
            "NFE expansion is not exact")
    nfe_path = "/WIPEOUT3/NFE.PB"
    require(nfe_path in iso_files and
            nfe_container.get("sha256") == iso_files[nfe_path]["sha256"] and
            nfe_container.get("bytes") == iso_files[nfe_path]["bytes"],
            "NFE container identity does not match verified media")
    nfe_entries = nfe.get("entries", [])
    require(nfe.get("entry_count") == len(nfe_entries) and len(nfe_entries) > 5,
            "NFE entry table is incomplete")
    for row in nfe_entries:
        payloads.append({
            "classification": ("authenticated-runtime-executable"
                               if row["index"] == 5 else "unclassified-payload"),
            "container": nfe_path,
            "container_sha256": nfe_container["sha256"],
            "encoding": row["encoding"],
            "expanded_bytes": row["expanded_bytes"],
            "expanded_sha256": row["expanded_sha256"],
            "index": row["index"],
            "inventory_sha256": nfe_digest,
            "static_mapping": "nfe-entry5-route-variant" if row["index"] == 5 else None,
        })

    require(authorization.get("schema") == "wo3se.executable-authorization.v1",
            "unsupported executable authorization schema")
    evidence = authorization.get("evidence", {})
    main_media = next((row for row in iso["files"]
                       if row.get("signature") == "ps-x-exe"), None)
    require(main_media is not None and
            main_media["sha256"] == evidence.get("exe_container_sha256"),
            "main executable is not bound to verified media")
    require(nfe_entries[5]["expanded_sha256"] == evidence.get("nfe_entry5_source_sha256"),
            "NFE entry 5 identity disagrees with authorization")
    require(finite.get("schema") == "wo3se.finite-indirect-targets.v1" and
            finite.get("image", {}).get("sha256") == evidence.get("exe_text_sha256"),
            "finite target evidence is not bound to the main executable")

    reports = {row["name"]: row for row in authorization.get("images", [])
               if row.get("name") in {"main-exe", "nfe-entry5-route-variant"}}
    require(set(reports) == {"main-exe", "nfe-entry5-route-variant"},
            "authorization lacks a game executable identity")
    finite_sources = {site["source"] for site in finite["sites"]}
    require(len(finite_sources) == len(finite["sites"]),
            "finite target sites are ambiguous")
    authorization_main_sites = {site["pc"] for site in reports["main-exe"]["unresolved_indirects"]}
    require(finite_sources.issubset(authorization_main_sites),
            "finite target site is absent from the authorization frontier")
    executable_images = []
    for name in sorted(reports):
        row = reports[name]
        unresolved = [site for site in row.get("unresolved_indirects", [])
                      if name != "main-exe" or site["pc"] not in finite_sources]
        categories = Counter(site["category"] for site in unresolved)
        executable_images.append({
            "authorized_instruction_count": row["authorized_instruction_count"],
            "authorized_ranges": row["authorized_ranges"],
            "base": row["base"],
            "callback_sites": [site for site in unresolved
                               if site["category"] == "argument_callback_target"],
            "compiled_region_count": 0,
            "direct_closure_complete": row["direct_closure_complete"],
            "end_exclusive": row["end_exclusive"],
            "finite_indirect_closure": row["finite_indirect_closure"],
            "identity_sha256": (evidence["exe_text_sha256"] if name == "main-exe"
                                else evidence["nfe_route_variant_sha256"]),
            "name": name,
            "rejected_direct_edges": row["rejected_direct_edges"],
            "supplemental_finite_sites": (finite["sites"]
                                          if name == "main-exe" else []),
            "unresolved_indirect_categories": dict(sorted(categories.items())),
            "unresolved_indirects": unresolved,
            "unknown_ranges": row["unknown_ranges"],
        })

    main_report = reports["main-exe"]
    payloads.insert(0, {
        "classification": "authenticated-main-executable",
        "container": main_media["logical_path"],
        "container_sha256": main_media["sha256"],
        "encoding": "ps-x-exe-text",
        "expanded_bytes": main_report["end_exclusive"] - main_report["base"],
        "expanded_sha256": evidence["exe_text_sha256"],
        "index": None,
        "inventory_sha256": iso_digest,
        "static_mapping": "main-exe",
    })

    epoch_rows = []
    previous_end = None
    for value, digest in sorted(epochs, key=lambda item: item[0]["runtime"]["guest_start"]):
        require(value.get("schema") == "wo3se.runtime-variant-inventory.v1" and
                value.get("source", {}).get("file_sha256") == nfe_entries[5]["expanded_sha256"],
                "runtime epoch is not bound to NFE entry 5")
        runtime = value["runtime"]
        require(previous_end is None or runtime["guest_start"] >= previous_end,
                "runtime epoch ranges overlap ambiguously")
        previous_end = runtime["guest_end_exclusive"]
        epoch_rows.append({
            "changed_word_count": value["delta"]["changed_word_count"],
            "compared_bytes": runtime["compared_bytes"],
            "guest_end_exclusive": runtime["guest_end_exclusive"],
            "guest_start": runtime["guest_start"],
            "observed_pc_count": value["observed_execution"]["pc_count"],
            "receipt_sha256": digest,
            "runtime_sha256": runtime["compared_sha256"],
            "status": value["status"],
        })

    unresolved_payloads = sum(row["classification"] == "unclassified-payload"
                              for row in payloads)
    unresolved_sites = sum(len(row["unresolved_indirects"])
                           for row in executable_images)
    rejected_edges = sum(len(row["rejected_direct_edges"])
                         for row in executable_images)
    unknown_ranges = sum(len(row["unknown_ranges"]) for row in executable_images)
    gates = {
        "all_media_payloads_classified": unresolved_payloads == 0,
        "all_static_regions_compiled": all(row["compiled_region_count"] > 0
                                           for row in executable_images),
        "callback_closure_complete": unresolved_sites == 0,
        "direct_edge_closure_complete": rejected_edges == 0,
        "media_expansion_complete": True,
        "runtime_epoch_coverage_complete": False,
        "unknown_code_eliminated": unknown_ranges == 0,
    }
    gates["content_static_coverage_complete"] = all(gates.values())
    return {
        "coverage_gates": gates,
        "executable_images": executable_images,
        "finite_target_manifest": {
            "receipt_sha256": finite_digest,
            "site_count": len(finite["sites"]),
            "sites": finite["sites"],
        },
        "inputs": {
            "authorization_sha256": authorization_digest,
            "iso_inventory_sha256": iso_digest,
            "nfe_expansion_sha256": nfe_digest,
            "pbp_extraction_sha256": extraction_digest,
        },
        "media_exclusions": [
            {"logical_path": row["logical_path"],
             "reason": "audio-track pseudo-file outside data-track image"}
            for row in sorted(excluded_audio, key=lambda item: item["logical_path"])
        ],
        "media_payloads": payloads,
        "runtime_epochs": epoch_rows,
        "schema": SCHEMA,
        "status": ("complete" if gates["content_static_coverage_complete"]
                   else "incomplete-fail-closed"),
        "summary": {
            "authenticated_executable_payloads": 2,
            "cataloged_payloads": len(payloads),
            "rejected_direct_edges": rejected_edges,
            "runtime_epochs": len(epoch_rows),
            "unclassified_payloads": unresolved_payloads,
            "unknown_code_ranges": unknown_ranges,
            "unresolved_indirect_sites": unresolved_sites,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--pbp-extraction", type=Path, required=True)
    parser.add_argument("--expanded-dir", type=Path, required=True)
    parser.add_argument("--nfe-expansion", type=Path, required=True)
    parser.add_argument("--authorization", type=Path, required=True)
    parser.add_argument("--finite-targets", type=Path, required=True)
    parser.add_argument("--runtime-epoch", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        iso, iso_hash = load(args.iso)
        extraction, extraction_hash = load(args.pbp_extraction)
        nfe, nfe_hash = load(args.nfe_expansion)
        authorization, authorization_hash = load(args.authorization)
        finite, finite_hash = load(args.finite_targets)
        expanded = []
        for path in sorted(args.expanded_dir.glob("*.json")):
            value, digest = load(path)
            expanded.append((media_path(path), value, digest))
        epochs = [load(path) for path in args.runtime_epoch]
        report = build_audit(iso, iso_hash, extraction, extraction_hash,
                             expanded, nfe, nfe_hash, authorization,
                             authorization_hash, finite, finite_hash, epochs)
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload, encoding="utf-8", newline="\n")
        print(payload, end="")
        return 0
    except (OSError, KeyError, TypeError, AuditError) as error:
        parser.exit(2, f"content coverage audit rejected: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
