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
  --json-out build/evidence/clean-aot-isolation.json
```

It scans every regular UTF-8 text file below the supplied source paths,
including build definitions, manifests, scripts, and documentation. It reports
title/serial contamination (including short `w3`/`wo3` aliases), suspicious
PS1 guest-address literals in hexadecimal or decimal notation, and references
to dirty interpretation, runtime compilation, or debug/trace infrastructure.
The build/test-only semantic CPU oracle is permitted in source but forbidden
from a release link map.

Inputs must resolve below `--root`. Source symlinks and Windows junctions,
control-byte/binary files, empty directory scopes, and non-UTF-8 files fail
closed. `.git`, Python `__pycache__`, and marker-qualified generated CMake build
directories are not source inputs. A generated build directory is skipped only
when its conventional name is paired with `CMakeCache.txt` or `build.ninja`.

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
required. `column` may be omitted only when the selector matches exactly one
finding. A record is one-to-one: ambiguous records, stale/unused records,
duplicate selectors (even with different reasons), traversal, drive-relative
or absolute paths, globs, unknown keys, and unknown symbols are rejected.

The short `w3` alias is intentionally conservative and can also be a legitimate
generic identifier. Such a use requires a line/column-specific reviewed
allowlist record; broad alias exemptions are not supported.

## Release link-map proof

```text
python tools/cleanroom/scan_release_link_map.py \
  --root . \
  --link-map build/release/release.map \
  --json-out build/evidence/release-link-map.json
```

The link-map gate first requires a structurally recognizable GNU ld, LLVM lld,
MSVC LINK/lld-link, or Apple ld64 map. Empty, header-only, arbitrary, binary,
and out-of-root files are malformed inputs rather than successful absence
proofs. The gate then checks the complete map text, including Itanium and MSVC
decorated symbol forms. Offline compiler/test-oracle targets, interpreter or
compatibility fallback, capture/JIT compilation, TCC APIs, and debug/oracle/
trace identities are release violations. Any raw forbidden-symbol hit makes
`forbidden_symbols_absent` and `status` false.  Link-map allowlist records are
retained only as an audit annotation; they can never turn a linked forbidden
symbol into an absence proof.

Receipts are sorted, compact one-line JSON with input SHA-256 identities,
detected link-map dialect, and no timestamps, root escapes, or absolute paths
when `--root` is supplied. Receipt creation uses exclusive-create semantics and
never overwrites an existing proof. Exit status is
`0` for a passing gate, `1` for a gate finding, and `2` for malformed inputs or
an invalid allowlist.
