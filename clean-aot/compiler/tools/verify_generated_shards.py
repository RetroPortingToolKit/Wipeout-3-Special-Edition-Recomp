#!/usr/bin/env python3
"""Fail-closed verifier for clean-aot-generated-shards-v1 directories."""

import hashlib
import re
import sys
from pathlib import Path


SHA256 = re.compile(r"[0-9a-f]{64}")
CASE = re.compile(r"case 0x([0-9A-F]{8})u")


def fail(message):
    raise ValueError(message)


def verify(root):
    receipt_path = root / "shards.manifest"
    lines = receipt_path.read_text(encoding="ascii").splitlines()
    headers = {}
    shards = []
    for line in lines:
        key, separator, value = line.partition("=")
        if not separator or not key or not value:
            fail("malformed shard receipt line")
        if key == "shard":
            fields = value.split(",")
            if len(fields) != 5:
                fail("malformed shard record")
            region, identity = (int(fields[index], 0) for index in (0, 1))
            block_count, instruction_count = (int(fields[index], 10) for index in (2, 3))
            if not SHA256.fullmatch(fields[4]):
                fail("invalid shard source SHA-256")
            shards.append((region, identity, block_count, instruction_count, fields[4]))
        elif key in headers:
            fail("duplicate shard receipt header")
        else:
            headers[key] = value
    required = {"schema", "variant_sha256", "image_sha256", "entry_table_sha256",
                "max_shard_instructions", "entry_count", "shard_count"}
    if set(headers) != required or headers["schema"] != "clean-aot-generated-shards-v1":
        fail("invalid shard receipt schema")
    if (not SHA256.fullmatch(headers["variant_sha256"]) or
            not SHA256.fullmatch(headers["image_sha256"]) or
            not SHA256.fullmatch(headers["entry_table_sha256"])):
        fail("invalid shard identity")
    maximum = int(headers["max_shard_instructions"], 10)
    entry_count = int(headers["entry_count"], 10)
    if int(headers["shard_count"], 10) != len(shards) or not shards:
        fail("shard count mismatch")
    identities = [record[1] for record in shards]
    if identities != sorted(identities) or len(identities) != len(set(identities)):
        fail("shard identities are not sorted and unique")

    expected_sources = {f"clean_aot_region_{identity:08x}.cpp" for identity in identities}
    actual_sources = {path.name for path in root.glob("clean_aot_region_*.cpp")}
    if actual_sources != expected_sources:
        fail("generated shard source set mismatch")
    all_pcs = []
    for _, identity, block_count, instruction_count, expected_sha in shards:
        if block_count <= 0 or instruction_count <= 0:
            fail("empty generated shard")
        if instruction_count > maximum and block_count != 1:
            fail("multi-block shard exceeds instruction bound")
        source = root / f"clean_aot_region_{identity:08x}.cpp"
        payload = source.read_bytes()
        if hashlib.sha256(payload).hexdigest() != expected_sha:
            fail("generated shard source hash mismatch")
        text = payload.decode("ascii")
        symbol = f'extern "C" bool clean_aot_region_{identity:08x}'
        if text.count(symbol) != 1:
            fail("generated shard symbol mismatch")
        pcs = CASE.findall(text)
        if len(pcs) != block_count:
            fail("generated shard block count mismatch")
        all_pcs.extend(pcs)
    if len(all_pcs) != entry_count or len(all_pcs) != len(set(all_pcs)):
        fail("generated entry PCs are missing or duplicated")

    table_payload = (root / "entry_table.cpp").read_bytes()
    if hashlib.sha256(table_payload).hexdigest() != headers["entry_table_sha256"]:
        fail("entry table source hash mismatch")
    table = table_payload.decode("ascii")
    declarations = re.findall(
        r'extern "C" bool clean_aot_region_([0-9a-f]{8})\(', table)
    if declarations != [f"{identity:08x}" for identity in identities]:
        fail("entry table shard declarations mismatch")
    packed_size = re.search(r"GeneratedPackedRegionEntry,(\d+)>", table)
    if packed_size is None or int(packed_size.group(1), 10) != entry_count:
        fail("entry table entry count mismatch")


def main():
    if len(sys.argv) != 2:
        print("usage: verify_generated_shards.py GENERATED_DIRECTORY", file=sys.stderr)
        return 2
    try:
        verify(Path(sys.argv[1]))
    except (OSError, UnicodeError, ValueError) as error:
        print(f"verify_generated_shards: {error}", file=sys.stderr)
        return 2
    print("verified clean-aot-generated-shards-v1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
