# Project deep review — 2026-08-30

This report records the code, bloat, performance, correctness, build, test, and
release-path review completed across the title repository and its pinned
`psxrecomp` framework. It is a point-in-time audit, not a substitute for the
current [handoff](../HANDOFF.md) or the product acceptance gates.

## Executive summary

The project has changed from a thin title configuration into a coupled product:
title-owned hardware and mod policy, generated/AOT inputs, a launcher and setup
flow, a large emulator runtime, a recompiler, debug tooling, and a guarded
release pipeline. The review followed those boundaries rather than assuming the
original project shape still applied.

The main result is a substantially leaner and safer release runtime. Measured
PE `.bss` fell from 369,817,120 bytes to 96,884,832 bytes: a reduction of
272,932,288 bytes (260.29 MiB, 73.80%). Release hot paths no longer record most
diagnostic rings, a 65,536-entry overlay fingerprint table is allocated only
when its diagnostic is requested, and product builds no longer pull test graphs
into their default build. Correctness fixes cover title RAM contracts, rematch
mod lifecycle, AOT overlay cycle retirement, code-generation cache identity,
standalone/product target separation, malformed-DLL probing, and source-driven
release validation.

The project is not free of bloat. The working directory contains 111.51 GiB
outside `.git`; 2,258 freeze artifacts larger than 1 MiB account for
102.54 GiB. Those files are ignored and ownership-sensitive, so this review
bounded future retention instead of deleting them. The largest runtime source
files remain monoliths, several live overlay tables should be measured before
further compression, and the local Release toolchain provides neither Vulkan
nor LTO.

## Scope and method

The primary investigation covered all tracked title and framework paths and
then followed generated, configured, and packaged edges that affect the shipped
program:

- title configuration, manifests, plug-ins, generated inputs, launcher assets,
  CMake, release scripts, verification tools, documentation, and CI contracts;
- the framework recompiler, code emitters, AOT/overlay compiler, runtime,
  renderer, devices, mod loader, debug server, launcher integration, tests, and
  setup packaging;
- tracked and ignored storage, large translation units, default build graphs,
  release PE sections/symbols, diagnostic capacity, hot-path producers, test
  registration, setup-source closure, and Git/submodule provenance.

Small increments used static or focused tests first. Full graphs and the product
build were deferred until they could prove the integrated state. No ignored
diagnostic corpus, user worktree change, or vendored source was deleted. Runtime
performance conclusions use compatible existing receipts plus structural and
binary evidence; this pass did not pretend that a build alone replaces a long
gameplay soak.

Before adding this report, the title tree contains 358 tracked entries and
5,316,263 Git-blob bytes (5.07 MiB). The nested framework contains 1,661 tracked
entries and 35,702,193 Git-blob bytes (34.05 MiB). Git-blob size is used here so
Windows line-ending materialization does not change the measurement.

## Architecture findings

The product has five operational layers:

1. The title layer owns game identity, an 8 MiB RAM contract, CPU/presentation
   policy, mod selection, generated guest code, launcher assets, and FMV policy.
2. The recompiler turns the title map into native base and overlay code and
   validates address, namespace, and code-generation contracts.
3. The runtime supplies the CPU/GTE/GPU/SPU/CD/SIO emulator, overlay loading,
   mods, input, audio/video, saves, debug services, and launcher lifecycle.
4. Build and test graphs combine title and framework under multiple standalone,
   launcher, debug, and lean-release configurations.
5. Packaging stages an executable plus a complete, reproducible, clean source
   closure from both Git repositories.

Most defects found during the review were boundary defects: a title compile
definition that did not reach runtime behavior, state reset that worked only on
initial launch, overlay DLLs relying on host globals, launcher-only code leaking
into standalone builds, source-contract tests drifting behind implementation,
or archive checks reasoning about only one repository. The refactors therefore
make ownership and target boundaries explicit rather than introducing another
generic abstraction layer.

## Bloat analysis and changes

### Release-resident diagnostic storage

The dominant shipped-memory bloat was debug history allocated unconditionally
in the Release executable. SIO byte/transaction/IRQ rings, audio taps, CD/SPU
history, device/parity rings, GL diagnostics, function tracing, and debug-server
diagnostic storage are now excluded or reduced to ABI-safe empty/minimal forms
when `PSX_NO_DEBUG_TOOLS` is enabled. Cross-translation-unit producer calls were
also replaced with release no-ops so the change removes recording work, not
only storage.

Exact PE evidence from the same Release configuration:

| Measurement | Before | After | Change |
| --- | ---: | ---: | ---: |
| `.bss` bytes | 369,817,120 | 96,884,832 | -272,932,288 |
| `.bss` MiB | 352.69 | 92.40 | -260.29 |
| Reduction | — | — | 73.80% |

The lean ABI still exposes query functions where callers depend on them, but
those functions return an empty result without retaining the underlying rings.
Debug configurations retain full diagnostics. The final DMA/CD-ROM history
increment accounts for 3,146,048 bytes of that saving and makes its five
producer paths compile-time no-ops in lean builds.

### Overlay diagnostics

The overlay fingerprint log previously reserved a 65,536-entry static table,
about 19 MiB by its source layout, regardless of whether logging was enabled.
It is now allocated on demand only when `PSX_OVERLAY_FP_LOG` is active and
degrades safely if allocation fails.

The largest remaining live overlay structures are not automatically bloat:
`s_lazy_man` is 10,747,904 bytes, `s_cand` is 6,029,312 bytes,
`s_lazy_range_links` is 4,194,304 bytes, `s_cache_idx` is 3,309,568 bytes,
`s_loaded_paths` is 3,145,728 bytes, and the range-link table is about 2 MiB.
They serve normal overlay discovery/loading, so the next step is high-water
instrumentation against representative titles before changing capacities.

### Source and build-graph bloat

An abandoned basic-block analyzer was removed, obsolete overlay-safety tests
were reduced to the unique live cases, and previously orphaned useful test
suites were registered. Product targets no longer make recompiler, runtime, or
standalone test executables part of the default build. Root-level build trees
and generated diagnostics are ignored consistently, and freeze-dump retention
is bounded for future runs (default four, hard maximum 64).

The disabled card-protocol diagnostic now writes to a process-unique OS
temporary directory and removes only its own files on both passing and normally
failing exits. It no longer leaves `test_dir` or synthetic card images in the
framework checkout.

The nested vendored test/doc/example/support corpus is 11,765,849 bytes
(11.22 MiB), or 33.0% of tracked framework blob bytes. `fmt/test` alone is about
1.93 MiB and `toml11/tests` about 0.40 MiB. This is repository weight rather
than product-runtime weight. It was retained because pruning third-party test
corpora without an update/vendor policy would make future upstream merges less
reliable.

### Workspace artifact bloat

The workspace outside `.git` is 119,732,528,432 bytes (111.51 GiB). Large
freeze files account for 110,096,992,206 bytes (102.54 GiB), concentrated in
the root and old release/debug build trees. This is the largest storage issue
by orders of magnitude. The files predate or belong to active investigations;
deleting them would be an ownership decision, not a refactor, so this review
left them intact and stopped future unbounded accumulation.

## Performance analysis and changes

The refactors improve startup/resident pressure and remove release hot-path
work:

- diagnostic ring writes and several out-of-line trace calls disappear from
  lean SIO, interrupt, trap, GL, CD/SPU, parity, audio, restore, and debug paths;
- overlay fingerprint storage no longer faults in or occupies address space
  unless explicitly requested;
- AOT overlay cycle retirement is DLL-self-contained, avoiding host-global
  coupling while charging cycles through the existing callback path;
- generated-code cache keys now hash complete inputs rather than allowing stale
  reuse after a content change;
- test graphs are opt-in, reducing normal configure/build work and invalidation;
- runtime dumps are retained within a bounded window rather than producing
  unbounded I/O and storage growth.

The release packager now dereferences the staged Windows executable before
stripping it, never applies a release-only transform to the canonical build
output, and fails if no configured/known strip tool can process the staged PE.
MSYS executable-suffix aliases are resolved by file identity without preferring
a stale `.exe` over a distinct extensionless non-Windows host. One canonical
Python ZIP writer emits sorted relative POSIX member names, including hidden
inputs and names with spaces, without the unsafe `./` prefixes produced by
native Windows `bsdtar`. The measured staged copy fell from 25,826,373 to
20,433,920 bytes, saving 5,392,453 bytes (20.88%); the source executable's
SHA-256 remained unchanged.

The current compatible product receipt remains the CPU-200, PGXP-off PAL x2
run: about 99.8 FPS / 1.00x over roughly 77 seconds, with presentation near the
expected 50 FPS. Earlier controlled work reduced a debug race frame from about
60 ms to 34–35 ms through native dispatch. These are prior receipts, not fresh
post-refactor gameplay acceptance, and old 900%/100% CPU or PGXP-on receipts are
not treated as current authority.

## Correctness and release-integrity refactors

The review also fixed defects that could not be classified as size or speed:

- runtime now consumes the title's 8 MiB RAM contract rather than relying on an
  inert compile definition;
- mod plans are reset and reactivated on lobby rematches as well as first boot;
- retired plug-ins no longer re-enter through a broad source glob;
- launcher-only FMV helpers are excluded from standalone targets;
- overlay cycle retirement no longer relies on private host globals;
- GPU DMA addresses fold through installed 2/8 MiB RAM before high-page
  uniqueness is applied, without changing the CPU physical-address mapper;
- AOT discovery accepts the current cache flavor, toolchain environment,
  address boundary, and optional static namespace;
- malformed overlay-shard probes use a thread-scoped Windows loader error mode,
  so the expected negative test does not raise a `python.exe - Bad Image`
  dialog;
- fatal recompiler validation reports a diagnostic instead of surfacing as a
  MinGW illegal-instruction failure;
- runtime and overlay source-contract tests were reconciled with live APIs;
- release profiles, nested profiles, title rebuild inputs, framework Git pins,
  unsafe ZIP paths, canonical/case/Unicode/device-name collisions, and complete
  source closure are checked before publication;
- setup staging accepts only tracked, clean title/framework inputs and release
  verification fetches required framework source before evaluating provenance.

## Maintainability risks

The runtime remains highly concentrated. `runtime/src/main.cpp` is 15,327 lines
and `runtime/src/debug_server.c` is 14,668 lines; together they are 29,995 lines,
22.9% of all tracked `runtime/src` lines. Other large subsystems include
netplay, GPU core/GL rendering, and overlay loading. These files mix policy,
state, protocol handling, and platform integration, which increases compile
invalidation and makes target guards easier to get wrong.

The safest decomposition order is by already-visible seams: launcher/FMV and
boot policy out of `main.cpp`; protocol handlers and diagnostic serialization
out of `debug_server.c`; then measured overlay indexes. Each move should retain
a narrow source-contract test and avoid a speculative framework rewrite.

## Verification evidence

The following evidence was collected after integrating the review increments:

- title suite: 79 tests passed, including 13 subtests;
- recompiler graph: 57 tests discovered; 55 passed, zero failed, two explicitly
  disabled (`l1_decoder_test`, `overlay_pair_dedup_runtime`);
- runtime graph: the complete build succeeded; CTest discovered 71 tests and
  passed all 68 enabled tests, with three explicit disables
  (`sio_dualshock_rumble_test`, `spu_fidelity_test`, `sio_card_protocol_test`);
- AOT pipeline: the full configured AOT test passed;
- product: the Release `psx-runtime` target configured, compiled, and linked
  with version `0.1.0`, `-O3 -DNDEBUG`, `PSX_NO_DEBUG_TOOLS=1`, and the 8 MiB
  title RAM contract;
- focused lean/runtime checks passed for overlay cycle retirement, diagnostic
  storage, function tracing, configuration registration, and interpreter/runtime
  performance guards.
- native Windows setup archive
  `dist/wse-0.1.0-codex-deep-audit-windows-x64-20260830.zip` contains 1,953
  sorted canonical members and is 38,054,589 bytes. Its complete title/framework
  source closure and framework pins pass the fail-closed freshness verifier;
  SHA-256 is
  `984f12a4472bd9292bfbfc2ebfc9d54d8098830e1f305355b0c7d904d2423d06`.

The local toolchain requested Vulkan but did not define `PSX_HAVE_VULKAN`, so
this executable uses the inert software fallback; LTO is also disabled by the
toolchain. The optional Beetle acceptance target could not run because
`libmednafen_psx.a` is absent. Neither limitation is concealed as a pass.

## Residual findings and next increments

1. **Ignored artifact ownership:** establish an explicit retention/archive rule,
   then remove the 102.54 GiB freeze corpus through a recoverable, reviewed
   operation. Do not treat a broad delete as routine cleanup.
2. **Runtime decomposition:** split `main.cpp` and `debug_server.c` along tested
   launcher/protocol/diagnostic seams.
3. **Overlay capacity:** add high-water telemetry to representative title soaks
   before reducing the remaining live tables.
4. **Toolchain capability:** make Vulkan and LTO availability explicit release
   prerequisites if those capabilities are required; the present configuration
   silently falls back or disables them.
5. **Vendor policy:** decide whether source-release completeness requires the
   full vendored test corpus before attempting repository-weight reduction.
6. **Acceptance coverage:** complete a longer moving-race soak, deterministic or
   listened CDDA proof, and the disc-identity resolution before public release.
   The disabled DualShock and card tests also retain known behavioral failures;
   making their protocols pass is separate from restoring their build coverage.

## Delivery state

The changes were committed in small, focused increments in both repositories.
At the end of the review both local branches remain intentionally divergent from
their upstream histories; a normal root push cannot fast-forward and a force
push would risk canonical history. In addition, the root pins a nested commit
that is not present on a remote-tracking branch, so pushing the title alone
would publish an unretrievable submodule revision. No push or public release was
performed. Integration must first deliver the nested history, then reconcile
the title's 110 remote commits without rewriting or discarding local work.
