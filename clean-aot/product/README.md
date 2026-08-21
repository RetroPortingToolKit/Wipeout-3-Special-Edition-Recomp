# Clean offline product pipeline

This directory is the policy and consumer boundary for a real static/region
recompilation build. It is not an emulator frontend and it never becomes a
runtime compiler. The pipeline consumes user-owned media in a temporary
directory, retains only hash/range/receipt metadata, and emits generated native
source outside the repository.

The pinned product identity is deliberately narrow:

- exact `SCES-02845` 14-track Redump BIN/CUE;
- exact `SCPH1001` BIOS (`71af94...e99d3`);
- the `dev` branch `pal_track01.xdelta3` at commit
  `ac46a5dc500a9581ea55b8efb5c1a0d4be944aba`;
- the resulting PAL Track 01 and PS-X EXE identities in
  [`product_profile.json`](product_profile.json);
- the one currently authenticated bounded runtime payload,
  `NFE.PB` entry 5, with its exact stored/expanded hashes and guest range.

No other PBP entry is executable merely because it is present on the disc. A
new payload or executable epoch must be added to the profile with exact source
identity, bounded range, and an independently reviewed region manifest. An
unknown identity fails closed.

## Commands

Dry-run is side-effect free and prints a canonical receipt:

```powershell
python tools/product/build_product.py dry-run `
  --cue "D:\media\WipEout.cue" `
  --bios "D:\bios\SCPH1001.BIN" `
  --patch "D:\patches\pal_track01.xdelta3" `
  --xdelta3 "C:\tools\xdelta3.exe" `
  --compiler "D:\build\clean_aot_compiler_cli.exe" `
  --regions "D:\regions\wipeout3se.regions.json"
```

`setup` performs the actual provenance CLI calls, applies the patch through
`tools/provenance/prepare_user_media.py`, cooks and inventories the patched
Track 01, inventories the exact PS-X EXE, inventories only the profile-listed
runtime payloads with bounded expansion, writes hash-only compiler/catalog
inputs, and invokes the offline compiler:

```powershell
python tools/product/build_product.py setup `
  --cue "D:\media\WipEout.cue" `
  --bios "D:\bios\SCPH1001.BIN" `
  --patch "D:\patches\pal_track01.xdelta3" `
  --xdelta3 "C:\tools\xdelta3.exe" `
  --compiler "D:\build\clean_aot_compiler_cli.exe" `
  --regions "D:\regions\wipeout3se.regions.json" `
  --output "D:\wipeout3se-build\setup"
```

`--compiler-interface clean-aot-v1` matches the current offline compiler CLI
shape (`--exe`, `--manifest`, `--output`) for PS-X EXE images. The
`clean-aot-image-v1` adapter is reserved for a compiler that explicitly accepts
bounded raw payload images; it is required before a runtime payload can be
compiled. The setup command never wraps a raw payload in a fabricated PS-X EXE
header.

The region file is mandatory for a real setup. It is the authorization boundary
for code/data ranges, roots, finite indirect targets, delay-slot ownership, and
shard limits. No Ghidra scan, capture, emulator, or aligned-word heuristic is
accepted as a substitute. The region file is intentionally not shipped here:
the current canonical compiler has not yet closed the complete WipEout CFG.

If an external clean runtime artifact is already available, `--artifacts`
plus `--artifact-root` also emits and validates an existing strict
`psxrecomp.clean-aot.catalog.v1` catalog. The artifact is measured in place;
media, BIOS, PS-X EXE, payload bytes, and generated sources are never copied
into the artifact root or the repository. Without artifacts, setup emits the
strict metadata-only `catalog-input.json`; `finalize` can later create the
catalog after the consumer's clean runtime target has been built:

```powershell
python tools/product/build_product.py finalize `
  --input "D:\wipeout3se-build\setup\catalog-input.json" `
  --artifacts "D:\wipeout3se-build\artifacts.json" `
  --artifact-root "D:\wipeout3se-build\bin" `
  --output "D:\wipeout3se-build\setup"
```

The `finalize` command calls the existing `psx_catalog validate` CLI before
publishing the catalog. The setup catalog-input carries the profile SHA-256;
finalization revalidates that binding and the complete pinned variant set
before it measures artifacts. Catalog selection remains exact and fail-closed;
it does not compile, interpret, capture-compile, or approximate a missing
variant. Use `--profile` on `finalize` only when finalizing against the same
explicit profile file used by setup.

## Output and safety contract

The output directory must be outside this repository. Setup creates it through
a sibling temporary directory and atomically renames it on success. Every
output path is checked for traversal, absolute/drive-relative spelling,
symlinks/junctions, portable-name collisions, bounded file counts and sizes,
and exact input hash reuse. Receipts contain no absolute user paths and no
media/BIOS/executable bytes. Existing outputs are never overwritten.

The generated `sources.cmake` is consumed by the template
[`CMakeLists.txt`](CMakeLists.txt). That CMake target contains only generated
native source and an explicitly supplied clean runtime target; it does not link
the compiler or any legacy/emulator component. Build outputs belong in an
external directory and are not repository assets.

## Current integration gaps

The product boundary is ready, but it cannot honestly claim a finished game
until the clean compiler has a reviewed CLI in the canonical worktree, a
complete region/CFG authorization for the main image, a raw bounded-image
compiler path for `NFE.PB` entry 5, and a clean runtime target that links the
generated sources. Those are explicit fail-closed inputs rather than hidden
fallbacks. The pipeline is designed so those interfaces can land without
changing media provenance or allowing emulator code into the final link graph.
