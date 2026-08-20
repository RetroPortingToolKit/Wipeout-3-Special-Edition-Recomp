# Cleanroom and release-isolation gates

These diagnostics are new cleanroom tooling. They do not import or copy the
legacy project's runtime interpreter, compiler, debug server, trace rings, or
oracle scripts.

## Source gate

Run the generic-source gate with stable paths rooted at the repository:

```text
python tools/cleanroom/scan_clean_aot.py \
  --root . \
  --source clean-aot \
  --allowlist tools/cleanroom/allowlist.json \
  --json-out build/evidence/clean-aot-isolation.json
```

It reports title/serial contamination, suspicious PS1 guest address literals,
and references to dirty interpretation, runtime compilation, or debug/oracle/
trace infrastructure.  Source exceptions are exact records only; there are
no wildcard paths, regular expressions, or global symbol exemptions.

The allowlist format is:

```json
{
  "format": "psxrecomp-cleanroom-allowlist-v1",
  "source": [
    {
      "rule": "guest_address",
      "path": "policy/verified_adapter.cpp",
      "line": 17,
      "match": "0x80010000",
      "reason": "reviewed and intentionally isolated"
    }
  ],
  "link_map": []
}
```

`rule` is one of `title_name`, `serial`, `guest_address`, or
`shipping_symbol`.  `path` is repository-relative and accepts either slash
style; it is normalized to `/`.  `line`, `match`, and a non-empty `reason` are
required.  `column` may be added when a line contains multiple identical
matches.  Duplicate records, traversal, absolute paths, unknown keys, and
unknown symbols are rejected.

## Release link-map proof

```text
python tools/cleanroom/scan_release_link_map.py \
  --root . \
  --link-map build/release/Wipeout.map \
  --json-out build/evidence/release-link-map.json
```

The link-map gate checks the complete map text, including GNU/LLVM, `nm`, and
MSVC-style symbol lines.  Any raw forbidden-symbol hit makes
`forbidden_symbols_absent` and `status` false.  Link-map allowlist records are
retained only as an audit annotation; they can never turn a linked forbidden
symbol into an absence proof.

Receipts are sorted, compact one-line JSON with input SHA-256 identities and
no timestamps or absolute paths when `--root` is supplied.  Exit status is
`0` for a passing gate, `1` for a gate finding, and `2` for malformed inputs or
an invalid allowlist.
