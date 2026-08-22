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
`clean-aot-image-v1` is the explicit bounded-raw-image adapter. The compiler
library and CLI now accept raw bytes only with manifest-supplied load, code, and
entry bounds. The compiler emits a variant-bound native entry table. The
setup command never wraps a raw payload in a fabricated PS-X EXE header.

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
before it measures artifacts. Successful selection also emits the validated
load range, dispatch roots, and executed ranges needed to construct the native
runtime binding. Catalog selection remains exact and fail-closed;
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
complete region/CFG authorization for the main image and a final product
executable/host target that selects the catalog variant and invokes the emitted
binding accessor. The bounded raw-image CLI, catalog binding receipt, generated
entry table, authenticated RAM loader, and fail-closed first dispatch now
exist. These are explicit fail-closed inputs rather than hidden fallbacks.

## Modern-PC release and validation lane

`build_product.py package` creates a new, atomic and relocatable Windows
directory from an explicitly supplied clean application executable, `SDL3.dll`,
and one or more generated native artifacts. Packaging is refused unless the
shipping sources and real release link map pass the clean-room gates. The
product profile is also used as a forbidden-hash set, preventing any pinned
disc, BIOS, executable, patch, or raw runtime payload from being relabeled as a
clean artifact.

```powershell
python tools/product/build_product.py package `
  --app build-release/Wipeout3SE.exe `
  --sdl build-release/SDL3.dll `
  --artifact regions/main.native=build-release/main.native `
  --root . `
  --source build-policy/shipping-sources `
  --link-map build-release/Wipeout3SE.map `
  --allowlist build-policy/shipping-source-allowlist.json `
  --output dist/Wipeout3SE
```

The source list and its exact allowlist are release-build artifacts matching
the final link graph. Passing broad source trees or the analysis-stage product
allowlist fails closed on unreviewed architecture constants and stale records.

The package contains only the application, SDL runtime, declared generated
clean artifacts, a hash manifest, a one-time first-run setup contract, and
portable configuration defaults. Disc tracks, CUE files, BIOS bytes, selected
executable bytes, setup receipts, compiler/oracle tools, legacy runtime code,
and absolute build paths are forbidden. First run imports and verifies the
player's lawful inputs into `${USER_DATA}/Wipeout3SE`; unknown identities are
rejected before runtime initialization. Saves, two controller/card slots,
input deadzone/hotplug policy, exact 44.1-kHz stereo audio, 100-Hz presentation,
and 4K/ultrawide defaults have separate bounded configuration sections.

After copying or extracting the directory elsewhere, remeasure every file and
reject additions or tampering:

```powershell
python tools/product/build_product.py validate-release --package D:/Games/Wipeout3SE
```

Initialize per-user state before authenticated media setup:

```powershell
python tools/product/build_product.py install `
  --package D:/Games/Wipeout3SE `
  --user-data "$env:LOCALAPPDATA/Wipeout3SE"
```

This creates two exact 128-KiB memory cards and the bounded modern controller,
44.1-kHz bounded audio-buffer/underrun-counting, 4K/ultrawide display configuration using atomic
write-through replacement. Its receipt remains
`awaiting-authenticated-media-import`; the portable `Wipeout3SE.cmd` resolves
the package from its own location and refuses to launch until the existing
authenticated setup/finalization pipeline has published `finalize.json`.

The complete first-run path invokes the existing hash-pinned media verifier,
patcher, bounded clean compiler, and strict catalog finalizer in one staging
directory. Only after every step passes does an atomic active-setup receipt
select the version; upgrades never mutate the prior setup in place:

```powershell
python tools/product/build_product.py first-run `
  --cue D:/Media/Wipeout3SE.cue --bios D:/Media/SCPH1001.bin `
  --patch tools/patches/pal_track01.xdelta3 --xdelta3 tools/xdelta3.exe `
  --compiler build-tools/clean-aot-compiler.exe `
  --regions build-policy/product-regions.json `
  --artifacts build-native/artifacts.json --artifact-root build-native `
  --user-data "$env:LOCALAPPDATA/Wipeout3SE"
```

Disc, patch, compiler, and source BIOS bytes remain outside the portable
package. Locally generated BIOS/game regions are copied into the versioned
per-user setup and locked to exact setup, finalize, catalog, artifact hashes,
and sizes. Setup retains no disc, patch, executable-image, or BIOS bytes; it
stores only a private path to the exact verified external user BIOS. Launch
remeasures that entire lock before starting the app and
atomically records the canonical bootstrap frontier:

```powershell
python tools/product/build_product.py launch `
  --package D:/Games/Wipeout3SE `
  --user-data "$env:LOCALAPPDATA/Wipeout3SE"
```

Any cross-version finalize receipt, modified region, missing region, or stale
active lock fails before launch. A recorded BIOS frontier remains explicitly
`frontier-recorded-not-game-ready`; it is evidence, not a game-ready promotion.
Failed processes and bounded launch timeouts write hash-only diagnostics to
`receipts/launch-diagnostic.json`. Setup upgrades atomically switch the active
version while preserving global configuration and both memory cards.
`rollback --user-data ... --version <prior-version>` remeasures an intact prior
generation before restoring it; stale generations leave the active setup
unchanged. After activation, relocated `Wipeout3SE.cmd` launches
`Wipeout3SE.exe --user-data` directly. Before activation it fails closed and
points to the BIOS/media selection contract in `policy/first-run-setup.json`.

The Windows package audit parses each PE import table directly, requires every
non-system DLL to exist in the package, reruns the shipping link-map leakage
gate, and binds its result to BIOS frontier evidence:

```powershell
python tools/product/build_product.py audit-windows `
  --package D:/Games/Wipeout3SE --link-map build-release/Wipeout3SE.map `
  --root . --frontier clean-aot/catalog/evidence/bios-frontier-00079405.json
```

`clean-aot/product/evidence/windows-installed-launch-audit.json` records the
current boundary. `CLEAN_AOT_APP_ENABLE_SDL=ON` now defines the real
`Wipeout3SE.exe` target. A release build supplies only an external directory of
locally generated clean region sources and its generated binding accessor;
the linker emits `Wipeout3SE.map`, and the target copies `SDL3.dll` beside the
PE. No BIOS, disc, patch, compiler, oracle, or legacy runtime is linked or
copied. The package audit consumes the authenticated 79,405-instruction v2
frontier. Installed smoke independently proves the executable's natural
71,501-instruction frontier using a separately supplied, hash-authenticated
user BIOS, then stops on the first unknown generated PC. The later authenticated
receipt includes staged RAM epochs and is not represented as a directly
reproduced installed boot.

The deterministic soak command runs a headless validation executable multiple
times with fixed locale/timezone/SDL drivers, requires identical output bytes,
enforces bounded execution, and checks a declared p95 ceiling:

```powershell
python tools/product/build_product.py soak --iterations 1000 `
  --timeout-seconds 30 --max-p95-ms 10 `
  --receipt build-release/receipts/soak-1000.json `
  -- build-release/product_soak.exe
```

The installed-product lane exercises the genuine packaged executable after its
authenticated BIOS frontier. Profiles `4k-headless`, `4k-hidden`,
`5120x2160-headless`, and `5120x2160-hidden` drive real host frame and 44.1-kHz
audio submission with exact deterministic 100-Hz deadlines. The harness rejects
late pacing, hard GPU submission errors, audio underruns, nondeterministic output,
timeouts, or a measured p95 above the supplied bound:

```powershell
python tools/product/build_product.py soak-windows `
  --package D:/Games/Wipeout3SE --bios D:/UserMedia/SCPH1001.BIN `
  --profile 5120x2160-hidden --updates 2 --iterations 100 `
  --timeout-seconds 30 --max-p95-ms 5000 `
  --receipt build-release/receipts/installed-ultrawide-hidden.json
```

Hidden-window swapchain deferrals are reported separately from hard GPU errors;
they are bounded by the update count and never presented as completed frames.

Passing these gates proves package isolation, relocation, deterministic harness
output, and the measured performance threshold only. It does not prove BIOS or
game boot, race completion, or release readiness.

`validate-release` is the executable full-product readiness decision, not only
a package checksum check. It remeasures the relocated package, hashes every
supporting receipt, writes a deterministic gate matrix, and exits 4 while any
objective is unmet:

```powershell
python tools/product/build_product.py validate-release `
  --package build-product-runtime `
  --evidence-root clean-aot `
  --receipt clean-aot/product/evidence/release-readiness.json
```

The four installed soak receipts feed a package-bound graphics receipt only
after every 4K/ultrawide profile reports all feature flags from the real
executable. Physical presentation timing remains `unavailable` unless every
profile supplies verified host completion evidence:

```powershell
python tools/product/build_product.py graphics-matrix `
  --package build-product-runtime-135829 `
  --soak clean-aot/product/evidence/installed-soak-4k-headless.json `
  --soak clean-aot/product/evidence/installed-soak-4k-hidden.json `
  --soak clean-aot/product/evidence/installed-soak-5120x2160-headless.json `
  --soak clean-aot/product/evidence/installed-soak-5120x2160-hidden.json `
  --receipt clean-aot/product/evidence/graphics-matrix.json
```

Content readiness is split into three independently measured contracts. The
content ledger binds every cataloged data payload and all 14 tracks to the
verified external multi-file CUE/BIN manifest without copying source media,
binds both authenticated executable identities to the complete 135,829 static
instruction set, and separately requires installed-product execution evidence
for every declared mode, track, team, ship, menu, FMV, CDDA, XA, and SPU route:

```powershell
python tools/product/build_product.py content-coverage `
  --content clean-aot/catalog/evidence/content-static-coverage.json `
  --shards clean-aot/catalog/evidence/generated-game-shards.json `
  --media-manifest assets/manifests/wipeout3se-media.json `
  --policy clean-aot/product/content-coverage-policy.json `
  --receipt clean-aot/product/evidence/content-coverage-ledger.json
```

The current receipt is intentionally `not-ready`: direct game instructions and
authenticated media reads have not occurred. External payload availability and
static executable coverage pass independently, while all routes without clean
installed-product execution evidence remain explicitly unexecuted. Graphics
and current-package soak gates pass.

The behavioral runner derives a deterministic 64-route, 100-Hz scripted-input
plan from the coverage policy. Each scenario begins with an authenticated game
boot barrier, then navigates the relevant menu or selection, exercises the
race, FMV, or audio route, and requests static-trace, frame, audio, and save
captures. The runner invokes only the installed `Wipeout3SE.exe` and binds every
capture to the package manifest, executable, external-media manifest, scenario,
and authenticated RAM epoch hashes:

```powershell
python tools/product/build_product.py behavior-plan `
  --policy clean-aot/product/content-coverage-policy.json `
  --receipt clean-aot/product/evidence/behavioral-scenario-plan.json

python tools/product/build_product.py behavior-run `
  --package build-product-runtime-135829 `
  --policy clean-aot/product/content-coverage-policy.json `
  --media-manifest assets/manifests/wipeout3se-media.json `
  --receipt clean-aot/product/evidence/behavioral-execution.json
```

At the current BIOS frontier the executable produces no eligible observation,
the command exits nonzero, and no execution receipt is written. Synthetic
harness fixtures test the protocol and hash verifier but are never accepted as
product evidence.
