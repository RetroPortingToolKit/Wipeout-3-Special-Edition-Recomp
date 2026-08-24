# WipEout 3 SE refactor handoff

## Current verified position

- Canonical root worktree is `master`; preserve the unrelated untracked
  `docs/internal/upstream/WIPEOUT3_SESSION_ENGINE_DRAFTS_2026-08-20.md`.
- Clean Release build passes `tools/verify_static_release.ps1` with the
  external retail BIOS boundary (`SCPH1001.BIN` supplied by the user).
- Direct launch with the PAL SCES-02845 cue reaches `game_started=1` through
  the recompiled BIOS path. The disc is not bundled.
- The currently supplied PAL Track 01 is already the target of the upstream
  PAL 25/50/100 patch: xdelta3 reports `source is already up to date` when
  applying `pal_track01.xdelta3` from upstream `dev` at revision
  `ac46a5dc500a9581ea55b8efb5c1a0d4be944aba`. The supplied executable also
  contains the `100HZ` title data. This authenticates the title input, but is
  not yet proof that the clean runtime drives its 100-Hz menu path.
- Debug RelWithDebInfo run verified the external HD pack is live: 445 indexed
  entries, 299 matched, 300 decoded images, 47,919 replacement hits, and zero
  decode failures.
- Normal CD timing probe recorded 3,840 exact data-sector deliveries, with no
  early/late/lost interrupts or response overwrites in the observed window.
- Disposable PAL-100 package probe now selected `wipeout3.framerate.pal-x2`
  against the authenticated patched media and logged PAL x2 / 100 Hz with a
  10 ms pacer through the real SCPH1001 BIOS path. Debug telemetry recorded
  3,841 exact data-sector deliveries (zero early, late, pended, or lost), 285
  native XA audio sectors, 1,340,640 CD-input frames, 2,219,248 SPU frames at
  44.1 kHz, and zero audio underruns. This is strong runtime timing evidence,
  but does not yet prove exact game simulation-update count or stable racing.
- The first deterministic windowed cadence probe is a failure, not a release
  receipt: after resetting the debug VBlank counters and measuring exactly
  10.004 seconds on a 100 Hz panel, the run reported 842 cycle-paced raises but
  only 526 guest deliveries (~52.6/s). Headless produced an invalid ~204/s
  result. The PAL-100 path therefore remains disabled until the title/menu
  cadence and presentation bottleneck are isolated and corrected.
- A follow-up perf diagnostic confirms the headless result is an uncapped
  diagnostic only: after PAL x2, the guest cadence varied above 100 Hz while
  the host pacer was intentionally bypassed. It does not prove presentation.
  The windowed 100 Hz result must be re-run with the render workload measured;
  do not convert the headless surplus into a PAL-100 release claim.
- The first host-policy correction is implemented in the runtime: an explicit
  native guest rate now keeps swap interval 0 and the wall pacer even when the
  panel nominally matches. This removes the observed PAL transition to driver
  vsync. The next 38-second windowed run held about 100 Hz before PAL x2, but
  settled around 60–70 Hz after PAL x2 with audio underruns; PAL-100 is still
  not release-ready because the full workload needs CPU-side optimization or
  a proven equivalent scheduling strategy.
- A clean `Release` rebuild (`-O3`) was also tested with the opt-in package.
  It improves the post-PAL interval to roughly 74–79 guest Hz, but still does
  not reach 100 Hz and still reports audio underruns. The earlier A/B with
  supersampling 1 and HD replacement disabled remained CPU-bound as well, so
  graphics presentation is not the sole cause.
- A link-time-optimization experiment was ruled out by the bundled compiler:
  its `cc1` has no LTO support. The Release build was restored to its original
  `-O3 -DNDEBUG` flags; no product behavior depends on the failed experiment.
- A disposable `-march=native` Release comparison was also ruled out: it
  measured only about 71–74 Hz in the PAL FMV workload versus roughly 75–79 Hz
  with generic `-O3`, so the bottleneck is structural runtime/generated work,
  not generic ISA selection. The build was restored to `-O3 -DNDEBUG`.
- FMV handling was corrected separately from gameplay cadence. Depth-24/MDEC
  presents now use a 50-Hz host period while PAL x2 gameplay remains 100 Hz,
  and the OpenGL FMV path receives the measured valid RGB width instead of
  forcing full-width sampling. Release rebuilt successfully and telemetry
  showed the FMV interval at approximately 50 Hz after the PAL gameplay
  interval; long-run audio/race verification remains open.

## Open blockers

- The PAL x2 activation initially exposed a boot-path defect: setting the native
  rate and CRTC multiplier immediately changed BIOS cadence before the title
  programmed its PAL mode. This is now corrected in the working tree: the
  native period and multiplier are deferred until a programmed PAL CRTC is
  observed. A 14-second boot probe records CRTC x1, guest ~60 Hz, SPU ~44.1
  kHz, and zero underruns. The subsequent PAL/FM V transition still falls to
  about 50 Hz with severe audio underruns, so gameplay/audio behavior remains
  unverified; the release profile now auto-selects it, but the runtime receipt
  still must prove sustained 100 Hz race behavior before shipping the EXE.
- FMV pacing was then corrected so PAL x2 no longer waits 20 ms on every guest
  tick. Depth-24/MDEC presentation now skips alternate PAL x2 presents while
  the guest remains on the 10 ms cadence. This raises the observed FMV-window
  guest rate from about 50 Hz to roughly 74–79 Hz and raises SPU production to
  roughly 33–35 kHz, but the full workload is still CPU-bound and audio
  underruns remain. The change is directionally correct but not a PAL-100 gate
  pass; further guest/render throughput work is required.
- A standalone Release comparison disabled the root project's forced netplay
  and rewind surfaces. With both optional systems off, the same PAL x2 FMV
  window reaches 99.7–100.2 guest Hz, about 44.1 kHz SPU production, zero
  underruns, and zero overflows. The alternate-present branch initially
  skipped pacing as well as presentation and falsely reached ~200 Hz; that
  was corrected so skipped FMV frames still receive the 10 ms guest wait.
  Root CMake now keeps netplay/rewind opt-in for this single-player product.
- With `auto_skip_fmv=true`, the title path
  reached generated game dispatch after PAL x2 and held approximately 99.9–
  105.4 Hz with approximately 44.1 kHz SPU production and zero underruns.
  This validates the title/menu-side workload, but no automated controller
  input has yet selected and completed a race, so race stability and full
  content coverage remain open. The release profile retains
  `auto_skip_fmv=true` as requested; the launcher can disable the feature.
- A corrected deterministic input route now reaches an actual on-track race:
  title → memory-card UI → main menu → vehicle class → ship → race track. The
  final capture is `race_probe.png` (disposable, not shipped). With FPS
  telemetry enabled, the on-track interval reports guest cadence about
  99–101 Hz. The SPU/CD/host taps remain 44.1 kHz; the final run records zero
  new SPU trace underruns, but the bridge's cumulative output-underrun counter
  remains nonzero after the long media/menu transitions. The GPU diagnostic
  `render` value is display-area flips, not swap cadence, and is not used as a
  presentation-FPS claim. FMV, race stability, and normal-vs-accelerated load
  equivalence therefore remain gated rather than declared complete. The
  temporary probe harness is being removed and the production build retains
  debug tools off, `auto_skip_fmv=true`, netplay off, and rewind off.
- The restored production executable passes the static Release boundary,
  `guest_clock_test`, and the authenticated PAL-title verifier. The build emits
  only existing third-party `stb_image` overflow warnings; no build or test
  failure occurred. PAL100 is now auto-enabled by the release profile as
  requested, while sustained gameplay timing remains an explicit open receipt.
- Focused FMV evidence is now available for both paths. Stock PAL remains
  PAL x1 at a 20 ms wall-clock cadence; opt-in PAL x2 remains a 10 ms guest
  cadence while the `gl_present_ring` shows movie swaps approximately every
  20 ms (50 Hz). The FMV screenshot is 320x240 and contains no text outside
  the movie bounds, confirming the corrected crop/clipping path. This is a
  cadence/crop pass, not a full content soak.

- Savestate/Hueponik-menu diagnosis: the Hueponik overlay is guest-side and is
  toggled by the guest Start+Select chord (keyboard Right Shift+Enter). The
  live host pad button word is not serialized in the guest snapshot, however,
  so a pre-load Start+Select latch could survive the restore boundary and
  reopen the overlay even when the saved race frame had it hidden. The runtime
  now clears every live pad word immediately after a successful savestate
  restore, before the existing input-release guard hands input back to the
  game. This preserves guest RAM/VRAM and timing while preventing host load
  input from becoming guest input. The same guard is now armed when submitting
  a save: Shift+Enter is the save-menu commit gesture, but it is also the
  guest's Select+Start chord, so the save path must consume it until both keys
  are released. Controlled debug-server save/load passes remain stable; the
  former report of a menu reappearing after a save is explained by this input
  collision rather than a savestate file corruption.

- Performance overlay integration is currently diagnostic, not yet a shipping
  decision. Linking the generated static PAL/high-detail corpus proved that
  exact relocated variants match (no variant or CRC misses), but the binary is
  very large and the hot interpreter fallback remains at physical PC
  0x007814D0, a CPS split continuation absent from the current static dispatch.
  The next performance pass must generate/dispatch that exact live block and
  then remeasure optimized Release at PAL x2/100 Hz; do not ship the broad
  corpus merely because it links.

- PAL100 title timing is now authenticated for the supplied PAL target: the
  title selector/state path at 0x8017BB8C–0x8017BBAC maps selector 2 to
  gSimVblankSkip=1 and PAL-x2 state=1. The opt-in PAL-x2 hook and target-only
  package are present, with `tools/verify_pal100_title.py` covering the exact
  local bytes and hashes. End-to-end PAL-x2 simulation, SPU/CDDA, timeout,
  media-load, and race traces remain open; keep the feature selected by the
  release profile but do not claim the runtime is proven until those traces
  pass.
- Normal-vs-accelerated memory-card and race-load equivalence is not proven;
  the framework's Fast Loading host-pacing mod is selected by default in the
  release state, while the two-gate equivalence harness remains fail-closed
  for proof purposes.
  The implementation is explicitly guest-observable-equivalent by design, but
  a paired normal/accelerated trace (CD sector/IRQ order, card writes, state
  fingerprint, and audio timing) is still required before treating that claim
  as verified for WipEout.
- The framework load-acceleration guard was repaired for the current source
  spelling/control-flow and now passes. It proves the static policy boundary
  (mod-owned, host-pacing-only, bounded, and separate from XA/FMVs), but does
  not replace the missing paired runtime trace.
- The first paired Fast Loading route exposed a real host-audio defect:
  4x host pacing produced 155,084 bridge overflow drops while the guest was
  accelerated. Fast Loading now automatically enables the existing
  discard-only turbo audio sink for multipliers above 1 and resets it on
  session reinitialization. The post-fix PAL-x2 route now completes the
  deterministic input sequence, holds roughly 99–101 guest Hz, keeps SPU/CD/
  host taps at 44.1 kHz, and reduces overflow drops to zero. It still reports
  substantially higher cumulative output underruns (and 54 SPU trace
  underruns) than the normal route, and the paired card/CD summaries have not
  yet been captured. Fast Loading remains enabled in the requested profile,
  but equivalence is not declared.
- A runtime probe of host-only DRC priming showed that it did not solve the
  accelerated transition: the bounded version reached zero overflow drops but
  still produced substantially worse bridge underruns. That experiment was
  removed; the original overflow-safe discard sink remains the baseline. The
  debug-only probe also exposed a missing Windows `iphlpapi` link dependency,
  which is now fixed in the runtime link line.
- The first disposable accelerated-route harness used a stale `input_hold`
  command and therefore did not drive the route. That harness was corrected
  to use the runtime's `input_route_append`/`input_route_start` API; the live
  probe confirmed the 21-step route was loaded, but it did not yet reach the
  race boundary before the probe was stopped. No aggregate audio result from
  that invalid/partial run is used as equivalence evidence.
- The corrected route was then allowed to complete all 21 steps. It captured
  32 sequential memory-card reads (sectors 0–31, all `data_idx=128`), guest
  SPU/CDDA taps at 44.1 kHz, and CD/XA trace activity through frame ~8055.
  This proves the accelerated path reaches the full menu/load/race route and
  preserves guest-clocked audio production. The host bridge still reports
  large aggregate underruns during the accelerated wall-time run, so host
  presentation smoothness and a byte-for-byte paired normal trace remain open.
- The frame-fingerprint interface was exercised on the clean normal prefix and
  returns per-frame RAM/MMIO hashes, write counts, and guest cycles. A paired
  accelerated fingerprint capture still needs to be retained and diffed as a
  durable artifact; the disposable probe was removed without treating its
  partial run as a pass.
- Runtime still links generic overlay/autocompile/interpreter plumbing even
  though the release configuration disables its behavior; strict link-surface
  removal remains unfinished.
- The current Release audit confirms the real `psx_interpreter.c` is not linked:
  native builds select `stub_interpreter.c`, and the staged tree has no BIOS or
  overlay/compiler cache artifacts. This is behavioral isolation, not yet the
  stricter requirement of removing every generic autocompile/provider object
  from the link graph.
- Widescreen/ultrawide and complete race/content soak validation remain
  unproven.

## Next concrete work

1. Diagnose the remaining accelerated-path audio underruns and capture paired
   normal/accelerated card and CD summaries (CD command/sector/DMA, memcard
   writes, state fingerprints, and audio timing).
2. Run the opt-in PAL100 title path and capture simulation, SPU/CDDA,
   timeout, media-load, and race equivalence traces.
3. Remove or prove dead all runtime compiler/interpreter paths in the release
   link graph, then rerun the static boundary gate.
4. Validate ultrawide/content coverage and perform a long deterministic soak.

- An isolated Debug build is now reproducibly instrumented with the user-supplied
  BIOS and disc passed explicitly at launch. The first durable paired
  `frame_fingerprint` probe completed, but the comparator failed at the short
  boot prefix: normal retained 34 entries while accelerated retained 32. This
  is diagnostic evidence only; it is not yet a causal guest-state divergence
  because the route ended before the menu/load boundary. Extend the same probe
  through card and race loading and identify the first common-frame mismatch.
- The first accelerated 6,000-frame route had PAL-100 enabled at the same time,
  so its 3,604-versus-3,597 fingerprint count is confounded by the intentional
  x2 cadence and is withdrawn as Fast Loading evidence. A subsequent
  Fast-Loading-only attempt reached only the 32-frame boot prefix, so it is also
  not a valid paired load result. Repeat the long route with Fast Loading on
  and PAL-100 explicitly off before drawing a timing conclusion. The CD
  comparator also includes run-local debug-global `seq`; normalize it to compare
  command, sector, IRQ content, and ordering without treating sequence numbers
  as guest state.
- The corrected Fast-Loading-only run (PAL-100 explicitly off) reached the full
  route. Its fingerprints match the retained normal run through frame 6,003;
  the first common-frame difference is at frame 6,004, but the two probes were
  armed at different post-boot anchors (normal ended at frame 6,012 while fast
  ended at 6,557), and the normal card ring was empty while fast retained 32
  reads. This localizes the remaining proof problem to probe synchronization,
  not a demonstrated Fast Loading failure. Re-run both sides after waiting for
  the same post-boot frame anchor, then capture card/CD traces from that anchor.
- A detached, file-backed synchronized probe now produced a usable paired
  window. Both runs were anchored at fingerprint total 629; the memory-card
  traces match exactly. Fingerprints agree through the shared window until
  frame 2,105, where the first RAM-write hash differs even though PC, write
  count, MMIO hash/count, SPU hash/count, and guest cycles still agree. The
  normal and fast route lengths were different, so endpoint counts are not
  comparable; the frame-2,105 divergence is the current narrow correctness
  boundary requiring a longer equal-route capture and CD/XA event alignment.
  The retained CD rings from that probe are also non-overlapping: normal
  contains only frames 3,619–3,633 while fast contains 2,092–2,106. Equal ring
  lengths therefore do not prove equal event order; treat the card match and
  fingerprint prefix as the only valid evidence from that run.

## Focused load gates

Fast Loading is tested at two explicit result points, not as whole-run frame
identity:

1. `startup-card`: compare the memory-card load endpoint. The required command
   is `tools/compare_guest_load_traces.py --gate startup-card --kind card`.
2. `race-load`: compare the loaded-race endpoint and a short continuation for
   deferred CD/IRQ/XA effects. Use `--gate race-load` with fingerprint and CD
   artifacts.

The comparator rejects a trace kind that belongs to the other gate. The current
anchored startup-card comparison passes; the race-load artifacts still need an
equal route length and overlapping endpoint window.

The runtime debug protocol now exposes `savestate_status`. Its monotonic
`generation` changes only after `savestate_poll` has applied or rejected a save
or load, and it reports `pending`, `last_ok`, `last_op`, and `last_slot`. A
focused load-gate harness can restore a checkpoint and wait for completion
before arming input, instead of relying on a sleep or an early staged-request
acknowledgement.

The save-state audit confirms this is the intended test shape: create one
pre-load checkpoint, restore it into normal and Fast-Loading branches, then
compare only the startup-card endpoint or race-load endpoint plus a short
post-load continuation. The status receipt removes the previous staged-ack
race; remaining harness work is calibrating committed pre-card/pre-race input
routes and expanding endpoint capture to card buffers, sector/DMA history, and
audio deltas.

## Ruled out

- Reusing the existing NTSC-120 package as a PAL-100 patch: PAL sites do not
  have the same instruction layout or timing constants; PAL100 now uses its
  authenticated title-side selector path and a separate PAL-x2 hook.
- Treating HD-pack discovery as proof of replacement: live substitution was
  separately measured and is now confirmed only for the observed 299 entries.
- Repeating an unbounded PAL-100 search without stronger runtime traces: the
  title-side candidate is now authenticated and selected by the release
  profile; simulation/SPU/CDDA/media comparisons remain required before the
  executable is treated as shipping-ready.

## Parallel GooseStation and remaster audits

The following scoped audits are now part of the canonical handoff:

- `docs/audits/goosestation-parity.md`: GooseStation/DuckStation-derived
  feature comparison. The current release has authenticated PAL x2, PGXP,
  900% overclock, HD replacement, BIOS skip, and save-state surfaces, but
  sustained PAL-100 proof, fast-loading equivalence, 21:9 native-frustum
  coverage, and Vulkan runtime parity remain open.
- `docs/audits/pacing.md`: PAL x2 produces 100 guest VBlank edges per second
  without doubling the CPU clock. The authenticated title selector must be
  selector 2 (`gSimVblankSkip=1`, PAL-x2 state 1) for 100 Hz simulation;
  FMV presentation remains approximately 50 Hz.
- `docs/audits/goosestation-skip-engine.md` and `docs/audits/boot-cd.md`:
  BIOS shell skip is structurally safe, but title equivalence is not yet
  proven. Generic card and pure-data CD skipping must remain separate, with
  CDDA/XA/MDEC/FMVs hard-excluded and race-load endpoints proven before
  default-on acceleration is claimed.
- `docs/audits/fmv-presentation.md`: FMV cadence and 4:3 bounded composition
  are separate from the 100 Hz gameplay path; remaining acceptance work is a
  full-window soak, not a gameplay-rate change.
- `docs/audits/hardware-parity.md`: 8 MB RAM and 900% CPU are applied in the
  observed runtime, but the resolver does not yet enforce them as an atomic
  title-hack pairing.
- `docs/audits/renderer-quality.md` and `docs/audits/hd-assets.md`: the
  OpenGL replacement path is live, but 98 expected HD keys have no PNG
  payload and the external pack is not yet release-gate validated.

The root CMake policy now defaults `PSX_ENABLE_LTO` to ON while honoring an
explicit OFF for toolchains without IPO support. A full UI-enabled product
build with LTO OFF compiled the runtime, PGXP, launcher, generated game code,
and plugins, then was stopped at the generated BIOS/static-overlay translation
units after excessive memory growth (approximately 11 GB). Do not repeat that
unbounded build as a performance test; use focused targets and the bounded
  native-continuation audit instead.

## Current release and agent findings

- The active release catalog is now intended to contain exactly six packages:
  8 MB RAM, Fast Loading, PGXP, Skip FMV, PAL x2/100 Hz, and widescreen. The
  static and one-folder verifiers reject unknown, retired, NTSC/120 Hz, legacy,
  duplicate, and nested retired package paths.
- `tools/verify_release_freshness.py` separately checks that an EXE or ZIP is
  newer than the relevant source/config and contains the exact six enabled
  packages plus the current 900%/PGXP/ultrawide/HD defaults. It correctly
  rejects the current stale `build-release` artifact.
- `tools/fast_loading_equivalence.py` now compares only the two requested
  endpoints: startup memory-card loading and race loading. It checks committed
  save-state restores, card traffic, CD sectors/traces/IRQs, guest frame
  fingerprints, and audio deltas/events. It now fails closed when the restored
  anchors/endpoints do not cover the same guest-frame window or when the
  post-anchor audio-event ring has wrapped, so an incomplete capture cannot
  report a false PASS. No live race-load equivalence receipt exists yet.
- `tools/verify_pgxp_runtime.py` reports PGXP configuration separately from
  runtime proof; it now reads the actual `pgxp_cpu_mode` setting rather than
  confusing it with the 900% CPU overclock. The PGXP save-state verifier covers
  reset, exact restore generation, and 4:3/16:9/21:9 continuation comparisons.
- The last shipped EXE and ZIP predate the latest native/PGXP/PAL100 changes;
  `build-main-pgxp` has no top-level EXE. A fresh bounded Release build remains
  required, but the generated static overlay is too large for an unbounded
  UI-enabled build in the current memory budget.
- A bounded incremental `build-release --target psx-runtime -j4` attempt
  recompiled the runtime, PGXP, BIOS, 64 generated game translation units, and
  PAL100 plugin without source errors, then stalled in the ccache/toolchain
  handoff during the final remaining work. It was terminated cleanly; the EXE
  timestamp and hash did not change, so the stale-artifact gate still fails.
- A follow-up `build-release --target psx-runtime -j1` attempt reached the
  final linker after compiling the full static overlay and all UI/runtime
  objects. The existing build-tree Release flag cache still injected
  `-flto=full` despite `PSX_ENABLE_LTO=OFF`, and `ld.lld` grew to roughly 9 GB
  without producing a new EXE during the bounded window; it was terminated
  cleanly. The next release attempt must use a fresh build directory or clear
  `CMAKE_C_FLAGS_RELEASE`/`CMAKE_CXX_FLAGS_RELEASE` before measuring the
  non-LTO path.
- The launcher audit found no safe parent-repo fix for “Play does nothing”:
  its pinned launcher/runtime submodule revision is unavailable from the
  configured remote, so the current path-normalization and sidecar contract
  must be repaired against a reachable matching revision before changing it.

## Latest bounded high-RAM performance pass (2026-08-22)

- The recompiler was rebuilt before regeneration; its baked code-generation
  hash matches the runtime headers. The retained high-RAM capture corpus has
  103 rows that executed `0x807814D0`: 96 exact native identities compiled,
  seven were safely skipped, and zero failed. The proof includes the exact
  prologue/range guard, `block_807814D0`, and a static dispatcher case.
- The target-only generated bundle is integrated as an optional extra static
  dispatcher, so the established 192 MB overlay translation unit remains
  unchanged. The runtime tries the established dispatcher and then the
  high-RAM dispatcher before interpreting dirty RAM. Duplicate exact static
  recipes are now removed fail-closed by `compile_overlays.py`; isolated
  fragment symbols in the local high-RAM bundle are separately namespaced to
  avoid collisions with the established corpus.
- The Clang Release `-O3 -DNDEBUG`, LTO-off build now links successfully. The
  current executable is 124,124,672 bytes with SHA-256
  `5BC2098F7E35F44F5BEEAA5062CF584B28644BB7B0CE5CC788FBE653AC9802E7` and
  contains `psx_overlay_dispatch_highram` plus the target entry symbols.
  Static release, one-folder release, freshness, PAL-100 title, and 900%
  timing-boundary gates pass. The one-folder gate was run after moving two
  stale user-state files out of the build directory; they remain preserved in
  `artifacts/` and are not release payloads.
- A fresh direct PAL probe reaches PAL-x2/100 Hz and confirms 8 MB RAM,
  PGXP-depth configuration, HD texture indexing, bezel staging, FMV skip, and
  the 10 ms gameplay pacer. This menu/title probe did not execute the high-RAM
  race continuation (`dispatch/s=0`), and its sustained guest cadence was
  approximately 93–95 Hz with substantial audio underruns. It is therefore
  evidence that the binary is wired and runnable, not a race-performance pass.
- The high-RAM native-versus-interpreter race A/B, 32-pass parity proof, and
  sustained 99–101 Hz race receipt remain open. Do not claim the performance
  target complete until a race save-state or equivalent deterministic route
  reaches `0x807814D0` and produces those measurements.

## Fresh release artifact checkpoint (2026-08-22)

- A clean product build now succeeds in `build-release-clang-nolto` using the
  bundled Clang toolchain, Release `-O3 -DNDEBUG`, and
  `PSX_ENABLE_LTO=OFF`. The resulting
  `Wipeout_3_Special_Edition__Recompiled.exe` is newer than all relevant source
  and configuration inputs.
- The fresh artifact passes `tools/verify_release_freshness.py`,
  `tools/verify_static_release.ps1`, and
  `tools/verify_one_folder_release.ps1`. The static boundary now intentionally
  accepts release `auto_skip_fmv=true`; the launcher may still disable that
  option for users who want the original FMV flow.
- The framework staging policy is now title-scoped: the one-folder release
  contains exactly the six intended packages—8 MB RAM, Fast Loading, PGXP,
  Skip FMV, PAL x2/100 Hz, and widescreen. Retired CD Speed, NTSC/120 Hz, and
  duplicate/experimental packages are not staged.
- The release now stages `bezels/wipeout3-neon.png`, a native one-folder bezel
  asset, and the one-folder verifier confirms its presence alongside the HD
  asset/font payloads.
- A fresh GCC non-LTO attempt reached the generated static overlay but grew to
  approximately 11.6 GB before it was stopped cleanly. The successful Clang
  build is therefore the current bounded release-build route; this is a build
  resource constraint, not a runtime-performance result.
- Remaining acceptance work is runtime evidence from the fresh executable:
  sustained PAL-100 race timing, visible PGXP geometry/depth correction,
  normal-versus-fast loading equivalence at both requested endpoints, actual
  HD font/texture replacement coverage, launcher Play behavior with user BIOS
  and disc paths, and Vulkan renderer parity. Packaging/static gates are no
  longer the blocker.

## Session closeout: race performance remains a hard failure (2026-08-22)

- The fresh debug race-state probe was run from
  `build-debug-tools/diag-race-run-2/scph1001/state_80162474_slot00.pst`,
  using the patched-header copy only. The original user state was not
  modified.
- With the PAL-x2/100 Hz path and the configured 900% CPU overclock, the
  race continuation did not run at playable speed. Recent windows fell from
  roughly 25 Hz toward 13--20 Hz while the guest worker consumed about
  990--995 ms/s; audio underruns increased continuously. This is the user-
  visible "about one-tenth speed" failure and is not a monitor-refresh or
  launcher-only problem.
- The high-RAM dispatcher is active and correct on its guarded identities:
  `highram_checks == highram_hits`, with zero variant misses. It materially
  reduces the debug race frame cost from approximately 60 ms to 34--35 ms,
  but it is not sufficient for the 10 ms / 100 Hz target.
- A live diagnostic A/B at 100% overclock rose to approximately 64 Hz on the
  same continuation, confirming that the 900% path is multiplying the amount
  of guest work the current host implementation must execute. It is not a
  valid product workaround because the requested Hueponik/GooseStation mode
  requires the 900% timing contract.
- Clean static-phase sampling identified `0x8015FA7C` as the dominant stable
  remaining hotspot (about 56--58% of static samples), followed by the
  `0x8017...` helper chain. `0x8015FA7C` is a short generated polling/syscall
  function whose loop repeatedly charges cycle timing and crosses BIOS/syscall
  edges; it must be optimized with an exact timing/IRQ-preserving path, not
  bypassed. Function tracing was used only diagnostically and is not a
  performance receipt.
- The current performance work is therefore intentionally not marked done.
  The next session should start with a clean Release race probe and an
  exact A/B optimization of `0x8015FA7C`/its callees, retaining PGXP, device
  deadlines, BIOS behavior, and the 900% overclock. Do not ship the current
  race result as a remaster-quality build.
- The debug-only `dispatch_stats` high-RAM counters and the
  `PSX_DISABLE_OVERLAY_EXTRA_DISPATCH=1` control switch are committed in the
  psxrecomp submodule to make the next A/B reproducible. No probe process is
  left running at session close.

## PGXP disabled and 900% idle-loop work (2026-08-23)

- The race-state title bar was rejected as an acceptance metric. Live RAM on
  the compatible race state proved `gSimVblankSkip=1` at `0x8017F02C` and the
  PAL-x2 state `=1` at `0x80175064`; this is not the false 100-VBlank/50-update
  divider case. The remaining failure is host throughput.
- A timing-preserving stack-countdown idle-loop recognizer now skips the
  `0x8015FAA4` poll across its syscall boundary while respecting the 900% Q16
  overclock carry and the next device deadline. The old `0x8015FA7C` hotspot
  fell from roughly 56--58% to 2.7% of static samples, but the race still does
  not reach 100 updates/s.
- Release diagnostics localized additional cost but not a complete solution:
  HD replacement off improved the same continuation from roughly 31 to 41
  displayed guest updates/s; disabling all PGXP-related paths added only about
  two more. Core translated geometry/render submission remains the dominant
  unresolved performance work. These rates are diagnostic only, not gameplay
  acceptance.
- PGXP output was visibly wrong in the user's race. It is now disabled in the
  game config, seeded mod state, framework package default, and release gates:
  geometry correction, perspective texturing, depth, and CPU propagation are
  all false. The package stays staged as an optional component. The freshly
  rebuilt Clang non-LTO portable release starts at 900% and PAL x2 without a
  PGXP activation message, and freshness/static/one-folder gates pass.
- Profile/config inputs are link dependencies now, preventing a successful
  executable build from leaving stale staged defaults. Two user-state files
  found in the release folder were preserved under `artifacts/` before the
  portable-layout verification.
- Performance is still not complete. Next work is a correctness-preserving
  optimization of the hot `0x801700F0`, `0x801704F0`, and `0x80171D0C`
  geometry path and the per-replacement GL submission path, followed by a
  wall-clock race-progress/audio receipt rather than title-bar telemetry.

## Overlay fragment/build consistency review (2026-08-24)

- The retained `artifacts/highram-fragment` experiment is not a runnable
  overlay cache: its directory contains only a patched C fragment and lock
  sentinels, with no `00780000_*.dll`. Its cache tag
  (`cg11_edf77f6b_gc0a23b42c_f0`) predates the current submodule/codegen state
  and must not be copied into a current cache or treated as runtime evidence.
- The current Release executable does contain `generated/overlays_highram.c`
  and the high-RAM dispatcher, but that generated source is ignored and was
  produced before the final `psxrecomp` changes that regenerated
  `overlay_codegen_hash.h`. Regenerate the bounded high-RAM source after the
  final recompiler/runtime commit before producing a shipping executable.
- `CMakeLists.txt` only consumes `generated/overlays_highram.c` when that
  ignored file already exists; the build graph has no generation recipe for
  it. A clean checkout therefore cannot reproduce the current high-RAM
  dispatcher from durable inputs. Keep this as a build/release blocker until
  the capture recipe or a durable generated-input contract is recorded.
- The existing high-RAM runtime logs do not close the performance proof:
  release/debug race probes recorded no high-RAM native dispatch, while the
  control remained around 29 guest Hz with millions of interpreted
  instructions per second. Static-release boundary verification passed, but
  the freshness gate correctly rejects the executable because current source
  timestamps are newer than the binary.

## Rewind performance A/B (2026-08-24)

- A hidden-cost hypothesis was tested against the clean-room PGXP candidate:
  builds with rewind support enable it by default and older logs confirm the
  aggressive `interval=15`, `fmv=4`, `depth=50` policy. This could plausibly
  serialize large 8 MiB snapshots often enough to contaminate performance
  measurements.
- The valid same-binary/same-state 30-second headless A/B used executable
  SHA-256 `47DFA7EFBF334814840CDA5F35E82A91D735D2433C087AFE024637419E684F71`.
  Both arms loaded slot 0 and resumed at `0x8016835C`; the only intended
  difference was default rewind-on versus `PSX_REWIND=0`.
- Rewind was not the primary bottleneck in this state. Rewind-on measured 28
  telemetry windows at mean/median 63.89/62.2 guest Hz; rewind-off measured 29
  windows at 62.30/61.9 guest Hz. The small negative delta is run noise, not a
  rewind benefit. Receipts are retained in
  `artifacts/rewind-ab-pgxp2-20260824/`.
- An earlier pair in `artifacts/rewind-ab-20260824/` is invalid because its
  cloned save directory had one extra nesting level and both arms reported
  `LOAD FAILED`; do not use its boot/headless telemetry.

## High-RAM overlay reproducibility contract (2026-08-24)

- `tools/generate_highram_overlay.py` now pins the retained proprietary input
  by SHA-256 (`0d5fa49ab033c759f18395a6e39c1c24aeb228dd8da6426dadb84c8b853e6808`),
  schema, row counts, bounded address/size, and observed execution/dispatch
  counts. An explicit `--capture` changes only the path and cannot bypass the
  identity checks.
- The wrapper generates through a same-filesystem temporary directory,
  validates the high-RAM-prefixed static symbols, and atomically publishes the
  result. `--check` compares without replacing it; compiler output is retained
  in a temporary log and only a bounded failure tail reaches the console.
- CMake exposes an explicit, fail-closed `generate-highram-overlay` target.
  It is deliberately not part of the default build and requires
  `-DWIPEOUT3_HIGHRAM_CAPTURES=<private capture>`; clean configuration remains
  possible without distributing the proprietary capture.
- The recompiler static generator supports a deterministic
  `--static-symbol-prefix highram` namespace, leaving default ABI names
  unchanged. Its final static bundle assembly streams source fragments without
  allocating a second translation-unit-sized string, and all CPS resume cases
  are planned and applied in one reconstruction per shard; block labels are
  indexed once rather than found by a whole-source regex for every
  continuation. Bounded exact diagnostic runs exposed and removed the prior
  quadratic source rewrites and final Windows page-churn path.
- Focused wrapper, namespace, and batched-resume tests pass. Exact end-to-end
  attempts exposed the scaling defects above, but the final corrected run was
  terminated externally before it flushed logs or created static output while
  two unrelated elevated evidence-build processes held roughly 32 GiB resident
  memory. Windows denied forced termination of those processes. Therefore no
  exact match/stale receipt exists yet: rerun `--check` after those elevated
  processes exit, and do not use the ignored generated source for a release
  until that check passes (or regenerate and then re-check).

## Renderer semantic tranche (2026-08-24)

- `0x801700C4` is the true private-ABI `SubmitTexturedQuad` entry;
  `0x801700F0` is its internal fast FT4 packet/OT-link path, not an independent
  callable function. Near-depth work tail-transfers to `0x80170B5C`.
- `0x80170254` is the callable leaf `ProjectPackedVertexPairs`, with two direct
  callers. It expands packed vertex-index pairs into projected 16-byte records
  and returns the shared screen-edge clip-code AND.
- `0x801704F0` is `BuildProjectQuadMidpoints`: it creates four edge midpoints
  and a center, projects all five, and stores SXY/depth/clip records.
- `0x80171AFC..0x801727E7` is `TransformAndSubmitMeshPrimitives`, reached by
  three small selector wrappers. It transforms source vertices into the clip
  cache, rejects fully clipped meshes, performs NCLIP/AVSZ processing, builds
  triangle/quad packets, and links them into the ordering table.
  `0x80171D0C` is an internal packet-build loop within this function.
- Evidence and register-convention details live in the corresponding
  `docs/audits/semantic-*.md` files. `symbols.toml` uses `[[block]]` for the two
  internal labels, and `psx_symbols.h` has been regenerated from callable
  `[[func]]` entries only.

## Indexed high-RAM closure and renderer tranche 2 (2026-08-24)

- The first exact run that survived all 644 capture rows and five duplicate
  recipe removals failed deterministically in
  `unresolved_static_variant_requests`: it rebuilt and materialized every
  aggregate function identity for every byte recipe, then raised `MemoryError`
  in `current_variant_func_ids`. This is a generator defect, not evidence that
  the retained generated source matches.
- The unresolved-demand pass now indexes variants once by dispatch address and
  validates only identities capable of satisfying each requested entry. A
  regression test includes an unrelated malformed identity and proves it is
  not inspected. The fix is psxrecomp commit `6c1bf67c`.
- A subsequent managed run completed all 91 primary-shard CPS rewrites but hit
  the command host's approximately 20-minute lifetime before final output.
  Synthesized shard sources are now written immediately to temporary spool
  files and removed from live metadata, bounding reconstruction memory;
  psxrecomp commit `b3384250`. The detached exact check using this version is
  running with unbuffered durable logs under
  `artifacts/highram-background-check-spooled.*.log`.
- The spooled run proved that synthesis itself is bounded, then was killed
  immediately after `Static duplicate recipes removed: 5`: the unresolved
  matcher still built an index for every aggregate variant before querying the
  small demand set. The index now admits only the union of requested dispatch
  addresses (`083d73cd`). That exact attempt used durable unbuffered logs under
  `artifacts/highram-background-check-demanded.*.log`.
- Demand-only indexing still materialized every matching identity for a
  demanded address. Exact closure now short-circuits on the first guard-valid
  identity and emits flushed phase markers (`dfcdefdd`); the current detached
  receipt is `artifacts/highram-background-check-shortcircuit.*.log`.
- `0x8012268C` is the sole-called `RenderRaceFrame` owner. Its generated output
  is split, but semantic control continues through `0x8012482B`; it owns race
  scene, vehicles, effects, HUD, and display synchronization.
- `0x801727E8` is the true callable `SubdivideAndSubmitMeshPrimitives` entry.
  It forces the selector argument and falls into the internal prologue at
  `0x801727EC`; the old callable symbol at `0x801727EC` was false.
- `0x80172E3C` is `BuildVisibleOrientedQuadList`. It projects and compacts
  accepted oriented quads into 20-byte scratch records, returning 0--47 or -1
  on overflow; the prior far clip-code packer name was incorrect.
- `0x8013535C` is `RebuildViewportClipCoefficients`, an argument-free leaf with
  one direct caller. It writes seven mode-indexed fixed-point outputs rather
  than merely four generic frustum bounds.
- `0x80135B8C` is the scalar-input twin
  `RebuildViewportClipCoefficientsFromScalars`; its six signed-16 arguments
  feed the same formulas and seven outputs, with `RenderRaceFrame` providing
  overridden view offsets.
- `0x801415F4` is `BuildVisibleObjectQuadPackets`: it filters 28-byte activity
  records, prepares transform state, consumes the compact visible-quad list,
  and emits/OT-links 40-byte `POLY_FT4` packets.
- `0x801422FC` is `SubmitSceneSectionFaces`: it installs the owner transform,
  submits a direct signed-index face list, then decodes packed run/catalog
  entries and invokes resolved object renderers polymorphically.
- `0x80162474` is the confirmed PS-X EXE header entry
  `PsxExeEntryTrampoline`, a two-instruction non-returning jump rather than the
  CRT body. The downstream loader probes expansion RAM, copies/relocates the
  high-RAM payload when available, and rejoins initialization at `0x8016247C`.
- `0x80172DDC` is `PrepareVisibleQuadTransform`: it installs the nested GTE
  transform, mirrors translation into background-color registers, and writes
  three coordinate offsets to scratchpad for `BuildVisibleOrientedQuadList`.
- The previously guessed clip sites/globals are now known: one shared side-axis
  coefficient plus distinct horizontal and vertical depth coefficients,
  produced by both viewport-coefficient builders and consumed at `0x80142634`.
- `0x80136078` is `InstallLinkedRenderTransform`: it follows `+0x44` links to
  the terminal transform (without composing the chain), builds a
  projection-adjusted scratch matrix, and installs GTE rotation/translation.
- `0x8016FC28` is `DispatchPackedFacePrimitive`. It decodes the face index,
  downstream count, and eight-way selector, installs callback words in
  scratchpad, and tail-jumps; it does not itself emit packets.
- `0x8016F94C` is `ProjectAndSubmitPackedFaceGroups`, processing 64-byte groups
  through `ProjectPackedVertexPairs`, common-outcode rejection, and 24-byte
  callback records. `ProjectPackedVertexPairs` emits an initial pair plus N
  stepped pairs (N+1 total), correcting the earlier cardinality note.
- `0x8016FA70` is `SubdivideAndSubmitPackedFaceStrip`; it projects adjacent
  pairs, rejects groups/quads, and midpoint/center-subdivides accepted quads
  into four callback submissions. `0x80170774` is its selector-3 two-level
  counterpart, producing up to sixteen leaf submissions per source quad.
- Address-taken callbacks `0x8016FD34` and `0x8016FEFC` are complementary
  textured-quad triangle fans: each projects a different edge midpoint and
  emits three FT3 packets, while `0x801700C4` emits one FT4.
- `0x80170B5C` is `ClipAndSubmitNearTexturedQuad`, a marshal/tail-delegation
  wrapper for the shared `ClipAndEmitSceneryPrimitive` at `0x80142634`. The
  latter is the 8,276-byte four-plane Sutherland-Hodgman implementation with
  XYZ/UV/color interpolation and FT3/FT4/GT3/GT4 packet paths.
- `0x80116128` is the argument-free, non-returning `RunRaceSession`: one-time
  race/render/audio initialization followed by the closed simulation/view/
  render loop containing the sole `RenderRaceFrame` call.
- `0x8012482C` is `InitializeRaceRuntimeResources`, called once by the race
  session. It creates the shared transform, allocates/publishes three runtime
  buffers, performs dependent initialization, and registers a callback.
- `0x80133B64` is `InitializeRaceViewportState`; generated entries at
  `0x80133D3C` and `0x80133F60` are internal continuation/epilogue blocks, not
  callable functions. Its logical span continues through `0x80134013`.
- `0x8016247C` is the non-returning `PsxCrtStartup`: BSS clear, stack/heap
  derivation, GP/FP setup, BIOS `InitHeap`, then transfer to `RunRaceSession`.
- `0x80140770` is `SubmitRaceSceneLayers`, the no-argument scene-pass
  orchestrator called from `RenderRaceFrame`; it conditionally dispatches the
  mode-specific pass and then invokes three ordered common passes.
- `0x8016E170` is confirmed data, not a callback: the NUL-terminated source
  path `c:\wipeout3\common\shads.bt4`. The broad generated range beginning at
  `0x8016CC0C` crosses embedded strings/tables and therefore overstates code
  coverage for this island. `InitializeRaceRuntimeResources` passes the path to
  `0x80141D6C` and actually publishes four allocations, including the allocator
  delay-slot result previously missed.

## Data-island correction and completed high-RAM receipt (2026-08-24)

- Static CPS closure now reuses already-built fragment guards across later
  snapshot demands and immediately synthesizes/spools each isolated fragment;
  psxrecomp commit `3ca4141d` removes the last repeated-fragment rebuild path.
- The pinned 644-row high-RAM corpus now compiles in about 20 seconds. A normal
  generation atomically published `generated/overlays_highram.c` at 20,133,914
  bytes, and an immediate independent `--check` regenerated identical output.
- `0x8016CC0C..0x8016F183` is a data island, not executable code: 235 printable
  NUL-terminated runs plus pointer/data tables occupy the interval. The prior
  `0x8016F180` seed was removed and the real `0x8016F94C` callable seed added.
- Generator hardening `61d89e28` rejects out-of-image direct jumps as
  clean-tail proof. Follow-up `4c791b51` removes positively classified data
  candidates whose only provenance is heuristic whole-image `jr $ra` scanning,
  while preserving forced/direct-call diagnostic stubs. A fresh base
  regeneration proves `0x8016CC0C` and `0x8016F180` absent from ranges,
  declarations, dispatch, and shard bodies; only real `F/R 8016F94C 124`
  remains.
- `0x8016C86C` is `PollMultitapControllerSlots`, the address-taken final SIO/pad
  stage ending at `0x8016CC00`. `0x80141D6C` is
  `InitializeShadowTextureAtlas`; its legacy path argument is ignored and it
  uploads the embedded fixed VRAM atlas. `0x801701B4` is
  `SetupAndProjectPackedVertexPairs`, a three-caller setup prefix that falls
  through into the independently callable `ProjectPackedVertexPairs`.
- `0x80170D3C` is `InitializeSceneryClipScratchpad`, the sole-called leaf that
  copies the 17-word renderer template to scratchpad and initializes clip
  workspace constants. `0x801719DC` is
  `CallSceneryClipperPreservingRendererContext`, the four-caller ABI bridge
  that checkpoints renderer-private registers, calls the shared scenery
  clipper, then restores context while retaining the updated packet cursor.
- `0x80112048` is the confirmed seven-instruction `SwapRgb24RedBlue` leaf;
  `0x80112064` is the adjacent `ByteSwapTwoU16Values` leaf. Together they
  normalize packed material colors/halfwords at the conversion sites around
  `0x80110C4C..0x80110CFC`, with mirrored expansion-RAM callers.
- `0x80141E5C` is `CreateTextureDescriptorFromImageData`, a four-caller public
  adapter that sign-normalizes its mode argument and delegates unchanged image
  data to the full descriptor/parser/uploader at `0x8014195C`.
- Coverage splits at `0x80140118`, `0x8014EE6C`, `0x80172940`, and
  `0x801731D4` are not callable ABI entries. They are now cataloged as internal
  blocks of, respectively, the owner at `0x80140000`, `SpawnRacerEffectRecords`
  at `0x8014EA88`, `SubdivideAndSubmitMeshPrimitives`, and
  `BuildVisibleOrientedQuadList`.
- `SpawnRacerEffectRecords` owns `0x8014EA88..0x8014F01B`, scans thirty
  96-byte slots, and initializes kind-specific racer effects through a 15-way
  jump table. Its three true direct calls are within the wrapper at
  `0x8015B3CC`; generated `0x8014EE6C` is only the kind-6 initialization arm.
- `0x8014195C` retains its original diagnostic-authenticated name
  `LoadTexture`. The 1,040-byte sole-called TIM loader accepts 4-bit/8-bit
  modes, allocates a 38-byte texture descriptor, derives CLUT/TPAGE and four UV
  pairs, and uploads palette/pixel rectangles through two `LoadImage` calls.
- `0x80170CBC..0x80170D3B` is the callable
  `ValidateAndRepairReverseOrderingTable`, a backward scan over packed 24-bit
  ordering-table links. Generated entries at `0x80170CD0`, `0x80170D04`, and
  `0x80170D24` are internal branch targets, not callable boundaries.
- `0x80137DFC` is the high-fanout `PlaySoundEffectById` adapter (81 direct
  calls): `-1` is a no-op, volume clamps at 230, ordinary IDs use the shared
  resource table/class 2, ID 7 uses its dedicated resource/class 18, and
  selectors 29/37/28 remap to metadata IDs 23/22/21 before the common voice
  allocator at `0x80121534`.
- `0x8011686C` is the confirmed shared `AllocateHeapBlock` implementation (85
  direct calls): align to four bytes, prefer a tail split with a 20-byte block
  header, maintain arena counters/main-heap low-water state, warn when no
  contiguous split exists, then fall back to consuming a whole free block.
- Racer-slot predicates index `0x801910E0 + index * 0x3A4` after rejecting
  negative indices. `IsRacerSlotVacant` at `0x8013491C` tests byte `+0 == 0`
  across 81 callers; `IsRaceRecordStatusZeroTypeOne` at `0x80134964` further
  requires byte `+3 == 1` across 61 callers.
- `0x80106350` is the confirmed 44-caller `EmitTexturedQuadPacket`: reserve 40
  bytes at `gPrimCursor`, emit a neutral-gray `POLY_FT4` with descriptor
  CLUT/TPAGE/UVs and caller rectangle, optionally select semitransparency or a
  one-texel UV footprint, and return the unlinked packet.
- `0x8011BDD0` is `GetMenuActionState`, the 49-caller input classifier for 12
  menu actions. It combines per-racer digital masks with validated `0x7300`
  analog records and a 93-count centered-axis threshold.
- `0x80136D0C` is `PlaySpatialSoundAtSegmentMidpoint`: 34 callers feed segment
  endpoints and listener context; it computes midpoint distance, wrapped
  heading-based left/right attenuation, clamps volume to 230 and pan to 0..30,
  then calls the shared voice allocator.
- `0x80104AA0` is `NextRandom15`, a 254-caller private-state RNG. It stores
  `(old >>s 1) XOR (old << 3)` to `gRandom15State` at `0x80173A94`, then
  returns the low 15 bits; adjacent seeded words are independent streams.
- Fixed-point trig uses 4096 angle units per turn and signed Q12 output.
  `SinQ12FromSignedAngle` (`0x8015C318`) and
  `CosQ12FromSignedAngle` (`0x8015C3E8`) fold through the private quarter-wave
  lookup and 1025-entry table at `0x8017A74C`.
- `0x8016192C..0x80161DFF` is `AccessSio1ControlState`, a 40-caller two-level
  selector over SIO1 STAT/MODE/CTRL/BAUD at `0x1F801050`, shadow state, baud
  division, and critical-section lifecycle. Fourteen generated interior
  entries are computed-jump labels, not separate ABIs.
- `0x8015C698` is the real variadic `FormatString` entry. Its 12-byte
  argument-spill prefix falls directly into generated piece `0x8015C6A4`; the
  combined 2,180-byte owner is sprintf-equivalent and has 57 direct callers,
  while the split body has none.
- `0x80121534` is the confirmed `AllocateAndQueueSoundVoice` sink. Its first
  argument is an integer descriptor handle, not a resource pointer. It manages
  24 sound slots and 24 voice records (ordinary 0..20, reserved 21..23), can
  allocate a second voice from class bit 8, applies Q8 pitch scaling, and
  submits initialized voices to the backend.
- `0x8017B90C..0x8017BBCF` is the sole-called
  `UpdatePerformanceTuningMenu`, owning the FRAME RATE/SHIP LOD/SHIP DIST/BIRDS
  developer menu and PAL-x2 publication. Generated entries `0x8017B920` and
  `0x8017BBA4` are interior instructions; their apparent overlay JALs are words
  in relocation-pair data, not executable callers.
- The text path is now joined: `DrawFontString` at `0x80105B78` maps letters,
  digits, punctuation and spaces across 69 callers, then delegates glyphs to
  `EmitFontGlyphPacket` at `0x80105CDC`. The latter selects one of four metric
  bands, emits/OT-links a 40-byte `POLY_FT4`, and advances the exact primitive
  cursor at `0x80198620` (correcting the prior catalog typo `0x801A8620`).
- `DrawResourceString` at `0x80113BEC` is a 41-caller transparent adapter over
  `gResourceStringTable[index]` into `DrawFontString`. The pointer table begins
  at `0x8017BF1C`, is runtime-populated and has observed accesses through index
  340; retained page-tail bytes are zero data despite generated `F/R ... E4`.
- `ApproximateVectorLength3i` owns `0x8014B614..0x8014B66F`; generated
  `0x8014B638` is an impossible-to-call internal continuation consuming live
  multiply results. It sends low-32-bit `x*x+y*y+z*z` to the Q12 lookup-based
  approximate square-root helper at `0x8015C488`.
- `VSync` at `0x8015F904` is the 45-caller root-counter/GPU-field timing API:
  negative mode queries software VBlank count, mode 1 queries Timer1 delta,
  and other modes perform stable-sample waits with GPU phase and watchdog
  handling through `0x8015FA7C`.
- `SetTransformTranslation` at `0x80135574` is a 33-caller six-instruction
  leaf that writes translation XYZ at `+0x14/+0x18/+0x1C` and marks transform
  state halfword `+0x40` as one.
- `BuildTransformRotationMatrix` at `0x8013578C` uses the recovered Q12 trig
  functions to write the complete row-major rotation
  `Ry(-a1) * Rx(a2) * Rz(-a3)` and marks transform state `+0x40`; 27 callers.
- `UpdateLoopingSoundChannel` at `0x8013670C` owns looping sound creation,
  stop/reset, 16-step volume slewing, and live pan/pitch/Q8-scale updates across
  28 callers, with selector validation on missing-handle creation.
- `ApplyGlobalRgbWithAlpha` owns `0x80122650..0x8012268B`: three callers pass
  a byte value, the wrapper binds global bytes `0x80175E71/+72/+70`, and calls
  `0x801220AC`. Generated `0x80122654` is a live-`a3` fallthrough block, not an
  ABI entry.
- The false zero-tail callable at `0x8017BF1C` required two analyzer fixes.
  `16e1fea7` rejects untrusted pointer aliases into eight-word zero runs;
  `b939445a` applies the same guard to mid-function branch splitting, where
  packed data word `0x11D011D2` at `0x801777D0` had fabricated the actual
  target. Exact base regeneration now has zero matches in ranges, declarations,
  dispatch, or bodies. Codegen hash `cb7b7863` high-RAM output was republished
  and independently reproduced from the pinned 644-row corpus.
- `DrainQueuedRenderObjects` at `0x80141F68` drains the queue/count at
  `0x801E2624/0x801762D0` in two passes, activates each transform, maps modes
  0/1/2 to the three wrappers entering `TransformAndSubmitMeshPrimitives`, and
  unconditionally clears the count; 23 callers.
- `ApproximateSqrtNonnegative32` owns `0x8015C488..0x8015C50B`; generated
  `0x8015C504` is its zero-return block. GTE leading-zero normalization indexes
  the 192-entry `floor(512*sqrt(n))` table at `0x8017AF5C`, then rescales.
- `Atan2Q12` at `0x8015C518` is the 35-caller signed fixed-point atan2 leaf:
  full turn `0x1000`, output `[-0x800,+0x800]`, using the 1025-entry table at
  `0x801786D8` with exact quadrant/axis folding.
- `CdControlWithRetry` at `0x8016118C` wraps the lower CD-ROM command core
  with command-specific reset/preflight behavior, temporary sync-callback
  suppression and at most four attempts; 14 callers, with exhausted retries
  normalized through the recovery helper.
- `WaitForVBlankCountWithWatchdog` at `0x8015FA7C` is the two-call helper inside
  `VSync`. It polls signed `gVBlankCount` with a `(scale << 21)` watchdog;
  timeout prints the retained diagnostic, calls `ChangeClearPAD/RCnt`, and
  returns early rather than resuming the wait (correcting the earlier audit).
- `DrawLocalizedFontString` owns `0x8010F3CC..0x8010F987` across 24 callers. It
  maps ASCII/punctuation and ISO-8859-1 bytes through the localized byte/glyph
  tables at `0x8016D634/0x8016D660`, emits glyphs directly, applies language
  bank `0x900/0x901`, and returns horizontal advance rather than a new string.
- `LoadPackedResourceBundle` at `0x80111AF8` parses a signed count and 32-bit
  size table, builds relocated prefix boundaries, obtains/publishes one base
  allocation, and passes the trailing payload plus base to `0x80115B1C`; 33
  callers. The allocator/codec subtype remains deliberately unresolved.
- `BuildTransformRxRyRzMatrix` owns `0x8013558C..0x8013578B`, writes the Q12
  row-major composition `Rx(a1) * Ry(-a2) * Rz(a3)`, preserves translation and
  marks state `+0x40`; 13 callers and six generated continuation entries.
- `IssueCdCommand` at `0x80168080` is the lower CD-ROM command core behind
  `CdControlWithRetry`, using hardware registers `0x1F801800..03`, table-driven
  parameter counts, sync/async modes, 8-byte responses and dual timeout/error
  handling. This corrects the earlier SIO interpretation of the wrapper.
- `AcquireCurrentArchiveEntry` at `0x8014B830` reads the current 12-byte
  archive record, returns direct data when policy permits, otherwise allocates
  through main/temp heaps and copies or decodes via `0x80115B1C`; 23 unique
  callers (one apparent extra was an overlapping-shard duplicate).
- `RegisterTextureBundleEntries` owns the pre-prologue at `0x80111DA0`, walks
  relocated pointers returned by `LoadPackedResourceBundle`, builds 38-byte
  texture descriptors, appends them to the global table/count, then releases
  the temporary bundle allocation; 24 callers, no interior calls.
- `ResolveModeDependentIndex` at `0x80117C08` is a 23-caller leaf: modes 2/0
  return index+8/index+14, mode 1 uses the eight-byte permutation snapshot at
  `0x8016DD60`, and other modes return one.
- `ProjectPointDeltaOntoNormalQ12` at `0x800ED8AC` is a frameless by-value o32
  aggregate leaf returning `dot(B-A,N) / sum((N_i*N_i)>>12)`, with GTE `SQR`
  component squares and denominator zero forced to one; 18 unique guest calls.
- `LeaveNestedInterruptCriticalSection` at `0x800EEE64` is the 22-caller pair
  of the recovered entry routine: its pre-frame load decrements nonzero nesting,
  otherwise the low-level leaf restores COP0 Status bits `0x401`.
- PsyQ-compatible graphics owners are now named: `PutDrawEnv` at `0x8015D68C`
  compiles/submits `DR_ENV`, caches the 0x5C-byte environment and returns it;
  `SetDefDrawEnv` at `0x8015F844` initializes clip/offset/texture/background
  fields with PAL/NTSC-dependent `dfe`. Both have 20 direct callers.
- `SetSoundSlotVolume` at `0x801224B4` validates audio enable, signed slot,
  24-entry bounds and active flag, stores byte volume at slot `+0x1A`, then
  dispatches the existing backend update; 20 callers.
- `CopyTransformMatrix` at `0x80135CDC` copies all nine rotation halfwords and
  three translation words without transposing, deliberately skipping MATRIX
  padding and containing-transform state; 15 unique guest call sites.
- `ReleaseAllTrackedTempHeapBlocks` owns `0x8014B96C..0x8014BA3F`. With a
  nonempty top index it releases the pending internal free-list block, pops
  all 12-byte offset records in reverse order, and frees their temp-heap
  blocks unless the heap-lock flag suppresses deallocation; the retained EXE
  has 23 direct callers (not the earlier inventory estimate of 24).
- `EnterNestedInterruptCriticalSection` at `0x800EEE18` is the 22-caller,
  no-argument entry half of a balanced critical-section pair. It uses COP0
  Status IEc plus the nesting word at `0x80173334`; the outer entry calls
  `0x8015F7F4` to clear Status bits 0/10, while nested entries only increment.
  The independent leave half begins at `0x800EEE64`.
- `PutDrawEnv` owns `0x8015D68C..0x8015D74B`: it optionally emits the retained
  `PutDrawEnv(%08x)...` diagnostic, compiles a 0x5C-byte DRAWENV into its
  embedded packet at `+0x1C`, submits 0x40 bytes through the active GPU driver,
  caches the environment at `0x80178F58`, and returns the input pointer. All 20
  direct calls are in the system graphics initializer at `0x800EDD68..DE44`.
- `FreeHeapBlock` at `0x80116DBC` is the 15-caller DFree implementation: exact
  payload lookup, free-byte restoration, and forward/backward coalescing with
  20-byte headers. `ReleaseHeapPrefixToMarker` at `0x80116D1C` is the related
  four-caller temp-heap rollback wrapper; it exposes reserved prefix space down
  to a marker (or the initial arena payload for null) and delegates the actual
  free/coalesce operation.
- `LoadFrontendTexture` owns `0x80110DF0..0x8011169F` in the frontend overlay
  and retains the original diagnostic name `LoadTexture`. It parses standard 4-bit and
  8-bit TIM block layouts, allocates a 0x26-byte runtime texture descriptor,
  auto-places supported rectangles, derives CLUT/TPAGE and UV bounds, uploads
  CLUT/pixels through the graphics transfer path, and returns null for modes
  other than 0/1. Its two callers are the paired bundle registrars using
  `(abr=1, variant=0/1)`.
- `CreatePersistentSoundSlot` owns `0x801217F4..0x80121A57`: nine callers use
  its returned flag-3 slot handle for ambient/looping updates; it initializes
  one or optional two ordinary queued voices and dispatches the slot, with
  exact non-rollback edge cases documented for exhausted voice allocation.
- `NormalizeVector3Q12InPlace` owns `0x80118FDC..0x80119137`: nine geometry
  callers pass mutable position/vertex deltas; it length-reduces by six bits,
  uses the approximate square-root helper, and writes an approximate Q12 unit
  direction in place, with zero/small-vector and overflow limits audited.
- `DecodePackedRacerControls` is the true frameless callable at
  `0x80102E70..0x80102FB7`: seven live/saved/stream input callers expand ten
  packed bits into racer record `+0x0C` action flags and receive packed bits
  `10..15` as a separate unsigned six-bit result.
- `CreateObjectInstance` owns `0x8012E408..0x8012E533`. It allocates a
  40-byte instance and a distinct 80-byte owned payload from the shared arena,
  copies the source name/scalars/payload, clears the new instance's runtime
  word at `+0x24`, and returns the clone. Twelve direct call sites and no
  interior entries were found; its retained failure text begins `New Instance`.
- `EmitCenteredTexturedSprite` owns `0x8011E4B8..0x8011E69B`. This call-free
  renderer emits one centered 40-byte semitransparent `POLY_FT4`, shades its
  caller RGB through GTE `DPCS`, copies a shared texture descriptor's UV,
  CLUT, and TPAGE fields, links by `depth >> 6`, and advances `gPrimCursor`.
  The executable has six direct calls and no interior call targets.
- `LoadImage` owns `0x800EEC80..0x800EED57`. This 16-caller Psy-Q wrapper
  submits `(RECT *, source_words)` immediately when the GPU transfer ring is
  empty, otherwise enqueues a 12-byte `-2/RECT/data` request in its eight-slot
  ring and waits through `DrawSync(0)` for its completion flag. Interrupt
  critical-section nesting protects both queue paths.
- `GetTPage` owns `0x80160274..0x801602AF`. Its 15-instruction leaf is the
  canonical Psy-Q four-argument TPAGE bit packer, has 25 direct callers and no
  interior targets, and touches neither memory nor hardware state.
- `SetSemiTrans` owns `0x8015FD4C..0x8015FD73`. This canonical Psy-Q helper
  toggles only primitive-code bit 1 at packet byte `+7`. All 57 executable JALs
  target the entry; none target its three internal branch/return blocks.
- `Printf` owns `0x8015CF48..0x8015CF57`, the 65-caller BIOS A0/3F formatted
  output trampoline. It preserves all incoming variadic arguments, contributes
  only the BIOS function selector, and returns the BIOS result unchanged.
- `Memcpy` owns the adjacent `0x8015CF38..0x8015CF47` BIOS A0/2A trampoline.
  All 34 executable calls target its entry, and it forwards destination,
  source, length, memory effects, and the destination return to BIOS unchanged.
- `SwapAdjacentBytes` owns `0x8012E310..0x8012E323`. It exchanges exactly
  bytes `p[0]` and `p[1]` in place, has 25 direct entry calls and no interior
  targets, and otherwise has no shared-state or hardware effects.
- `ReverseFourBytes` owns the adjacent `0x8012E324..0x8012E347` leaf. It
  reverses exactly four bytes in place, has nine direct entry calls and no
  interior targets, and leaves all other state untouched.
- `ExitCriticalSection` owns `0x80165550..0x8016555F`. This fourteen-caller
  syscall-2 wrapper enables COP0 Status IEc and IM2 and returns zero; it has no
  RAM effects or interior entry targets.
- `AbsoluteValue32` owns `0x8014B3D4..0x8014B3EB`. It is a 21-caller signed
  integer absolute-value leaf with ordinary two's-complement wrap at
  `INT32_MIN`; no call targets either internal branch/return block.
- `SetPolyFT4` owns `0x8015F7A4..0x8015F7B7`. It initializes only the length
  (`9`) and opcode (`0x2C`) bytes of a 40-byte opaque `POLY_FT4`; all 20 direct
  calls target the single entry.
- `EmitLocalizedFontGlyphPacket` owns `0x8010EAC4..0x8010F247`: it selects
  six-byte glyph metrics and font texture state for height modes 8/9/16/18 and
  extended banks, builds and OT-links one 40-byte `POLY_FT4`, increments the
  packet cursor, and returns the next X pen. It has 15 direct callers and no
  external interior targets.
- `DrawSignedDecimalInteger` owns `0x80110560..0x801106CF`: it emits an
  optional minus sign and up to five low decimal digits, supports caller-set
  leading-zero width (observed zero/two), remaps low font styles in global mode
  2, and returns the final X pen. It has ten direct callers and no interior
  entry targets.
- `AccumulateIndexedVertexWeights` is the frameless leaf at
  `0x8014F920..0x8014FA4B`: it locates a flagged 24-byte geometry record from an
  object's `+0x34` index, converts positive byte `+0x3F` into fixed additions
  on three indexed 12-byte vertex accumulators, and consumes the byte to zero.
  It has nine direct callers and no external interior targets.
- `ResolveObjectTrackSurfaceCollision` owns `0x800ECB6C..0x800ED48B`: it
  selects flagged 24-byte geometry records from an object's `+0x34` index,
  performs adjacent-surface fixed-point/GTE intersection tests, and dispatches
  response helpers according to force mode, object flags, and prior-hit state.
  It returns `-1/0/1`, has seven callers in `0x800ED48C`, and no interior alias.
- `CdControl` owns `0x80160F1C..0x80161057`: it suppresses/restores the CD sync
  callback, performs status-bit command-1 and table-driven command-2 preflight,
  retries the requested `IssueCdCommand` transaction up to four times, and
  returns boolean success. It has 12 direct callers and no interior alias.
- `CdControlF` owns `0x80161058..0x8016118B`: it performs synchronous recovery
  and preflight with the sync callback suppressed, then restores the callback
  and submits the requested command through `IssueCdCommand(..., async=1)`.
  It retries immediate failures four times, returns boolean acceptance, has
  nine direct callers, and no interior alias.
- `DrawSync` owns `0x8015D240..0x8015D2A7`: it optionally emits the retained
  `DrawSync(%d)...` diagnostic for GPU driver types >=2, then calls the active
  driver's vtable `+0x3C` method with mode and returns its result unchanged.
  It has 13 direct callers and no interior alias or direct MMIO access.
- `LoadImage2` owns `0x8015D454..0x8015D4B3`: it validates/logs the RECT under
  the retained `LoadImage` operation name, then calls the active GPU driver's
  vtable `+0x08` method as `(driver+0x20 context, rect, operation 8, pixels)`
  and returns its result unchanged. It has five callers and no interior alias.
- `DivideSignedByPowerOfTwoTowardZero` is entered at `0x8014B6A4` and returns
  signed `value / 2^(shift&31)` with truncation toward zero. Its generated
  `R ... 14` prefix branches into `0x8014B6B8..BF`; that generated 8-byte F/R
  is an internal positive return suffix with zero direct callers. The real
  entry has 53 executable callers (plus two opcode-shaped data words) and
  semantic extent through `0x8014B6BF`.
- `WrapAngle4096Signed` at `0x8014B6C0` masks to 12 bits and returns the signed
  representative in `[-2047,2048]`. Its semantic extent includes generated
  `F/R 8014B6D8 8`, an internal nonnegative return suffix with zero callers.
  There are 17 unique guest call sites; 26 generated instances reflect four
  mutually exclusive copies of three overlay call addresses.
- `DotProduct3Int32` is the unsplit frameless leaf at
  `0x8014B4F8..0x8014B543`: it returns the wrapping low-32-bit sum of three
  signed products supplied as `a0..a2` and stack arguments 5..7, with no scale
  or saturation. It has ten unique guest callers (12 overlay instances) and no
  interior target or global/hardware effect.
- `ComputeCrossProductShift9` is the unsplit `0x8014B544..0x8014B5D7` leaf:
  it writes `wrap32(A x B) >> 9` to three destination words, with the wrap
  occurring before the arithmetic shift. It has one unique guest caller (two
  overlay instances); its residual `v0 = out.z` is not consumed.
- `SubtractVector3Int32` is the unsplit `0x8014B5D8..0x8014B613` leaf: it writes
  the wrapping componentwise difference `B-A` from scalar register/stack
  arguments to a caller-supplied three-word destination. It has nine unique
  guest calls (12 overlay instances), all in geometry owner `0x800EBDF0`.
- `ApproximateArccosQ12` is the unsplit `0x8014B670..0x8014B6A3` leaf: it
  indexes the 4097-halfword table at `0x8017634C` by absolute Q12 cosine and
  reflects negative inputs around pi=`0x4000`. It has nine direct callers, no
  overlay duplicates/interior target, and requires input in `[-4096,4096]`.
- `ComputeRaceFrameRandom15OffsetA00` is the unsplit
  `0x80104A48..0x80104A73` leaf: it mixes the selected-racer seed plus race
  frame counter plus `0xA00`, returns the low 15-bit sample, and stores no
  state. It has nine unique callers (ten overlay instances).
- `AccumulateOneIndexedVertexWeight` is its single-step companion at
  `0x8014F814..0x8014F91F`: it performs the same flagged-record lookup and
  fixed 250/200/200 indexed accumulator update once, then decrements positive
  byte `+0x3F` by one. It has six direct callers in the track-neighbor update
  loops and no external interior jump targets.
- `ApplyRacerDamageEvent` owns `0x801562B8..0x80156B8B`, with 12 direct entry
  calls and no interior JAL targets. It maps selected event codes (1/2/3/5,
  0xC..0x10, 0x80..0x83) to configured deltas/effects or accepts an explicit
  delta, applies racer/mode scaling, updates/clamps per-racer Q8 damage and
  percent state, manages hit timers, and performs the 100-percent disabled
  transition plus the 76-percent crossing effect.
- `FindNearestTrackSection` is the call-free owner at
  `0x8012ED50..0x8012EFAF` with 12 direct callers and no interior targets. From
  a seed node and XYZ point it scans a mode-sized window of linked 0x3C-byte
  track-section records, minimizes squared distance after `>>4` coordinate
  scaling, optionally searches a flag-selected branch chain, and returns the
  closest section without modifying shared state.
- `TrySpawnRandomGeometryParticle` owns `0x8011E69C..0x8011E963`, has one
  direct caller and no interior call targets. It passes a mode and 1-in-16 RNG
  gate, pops one of two free-index stacks, and initializes a 48-byte type-3
  effect record around a geometry-selected vertex with randomized position,
  motion/intensity fields, lifetime 40, and its source-owner pointer.
- `ReleaseSoundSlot` owns `0x80121A58..0x80121B3B`, is a frameless leaf with
  ten direct callers and no interior call targets. It validates a retained
  slot handle, ownership-checks its primary/secondary queued voices, clears
  owned voice control/active words and the slot state, then ORs their bits into
  the deferred audio-backend mask at `0x80175E9C`.
- `HandleErrorSeverity` owns `0x800F82EC..0x800F832B`, has 31 direct callers
  and no interior targets. It ignores the diagnostic pointer, sign-normalizes
  severity to 16 bits, returns unless severity is exactly 2, and then invokes
  BIOS B0 service `0x38` (`exit`) with status zero. All executable callers pass
  severity 2; no retained historical function spelling is proven.
- `DelaySpuRegisterAccess` owns `0x8016B6F8..0x8016B753`, has 14 direct callers
  and no interior targets. It is a no-argument 60-iteration multiply busy loop
  with stack-only state; adjacent SPU-library callers use it around control,
  transfer-port, and status accesses. It has no direct MMIO/global mutation or
  semantic return, and no historical private-library spelling is claimed.
- `ArmTimer2Timeout` owns `0x8016C51C..0x8016C53B`, is an eight-instruction
  leaf with 12 direct callers and no interior targets. It records the caller's
  threshold at `0x801EEBC4` and a 16-bit Timer 2 counter snapshot from
  `0x1F801120` at `0x801EEBC8`; the adjacent poller handles wrap, timer mode,
  and expiry. The arming leaf does not configure hardware or return a value.
- `HasTimer2TimeoutExpired` owns `0x8016C53C..0x8016C5DB`, is a frameless leaf
  with seven direct callers and no interior targets. It corrects one Timer 2
  wrap using target `0x1F801128` (or `0x10000` when zero), applies `>>3` elapsed
  scaling when mode bit `0x200` is clear, and returns canonical unsigned
  `elapsed >= threshold` without mutating the saved timeout state.
- `Strcpy` owns `0x8015F614..0x8015F623` and is the four-word BIOS A0/19
  trampoline, including one unreachable padding NOP. It has 20 direct entry
  callers and no interior targets, preserves destination/source and caller
  `ra`, and returns the BIOS destination pointer unchanged; all byte-copy,
  terminating-NUL, capacity, overlap, and fault behavior belongs to BIOS.
- `Toupper` owns `0x801602B4..0x801602C3` and is the four-word BIOS A0/25
  trampoline, including one unreachable padding NOP. Its sole caller at
  `0x80113438` passes a zero-extended byte and stores the low converted result
  byte; there are no interior targets, and the thunk preserves `a0`, caller
  `ra`, and the BIOS integer return without direct state effects.
- `CdGetToc` owns `0x801602C4..0x801602E7`, has one direct caller and no
  interior targets. It forwards the caller's TOC output pointer as `a1`, sets
  internal selector/session `a0=1`, calls the `0x801602E8` GetTN/GetTD worker,
  and propagates its track-count/zero result unchanged. Device/output effects
  belong to the worker; the wrapper itself only uses a 24-byte frame.
- `CdGetToc2` owns `0x801602E8..0x8016050B`, has two direct callers and no
  interior targets. It ignores its first argument, suppresses/restores the CD
  sync callback, issues GetTN then GetTD(lead-out/each BCD track), writes
  minute/second/zero records at four-byte stride, and returns the inclusive
  track count or zero. Failures retain any earlier output and optionally print
  the worker's retained `CdGetToc2: error` diagnostic.
- `CdSyncCallback` owns `0x80160EF4..0x80160F07`, is a five-instruction LibCD
  leaf with 12 direct callers and no interior targets. It loads and returns the
  previous callback from `0x8017B124`, then stores the incoming pointer in the
  return delay slot. Null suppresses callback dispatch; the exchange performs
  no validation, locking, interrupt masking, or nested-state management.
- `CdReadyCallback` owns `0x80160F08..0x80160F1B`, is the adjacent
  five-instruction LibCD exchange with nine direct callers and no interior
  targets. It returns the previous ready callback from `0x8017B128` and stores
  the incoming pointer in the return delay slot. Ready-event dispatch is
  independent from sync word `0x8017B124`; null suppresses it without locking.
- `CdDataCallback` owns `0x80161338..0x8016135B`, has nine direct callers and
  no interior targets. It forwards the callback to dynamic DMA dispatcher
  `0x801625EC` with fixed CD-ROM channel 3 and propagates the replaced callback
  result. Callback storage/hardware effects belong to the active DMA backend;
  the wrapper itself only uses a 24-byte frame and does not touch ready/sync
  callback globals.
- `DMACallback` owns `0x801625EC..0x8016261B`, has seven direct callers and no
  interior targets. It preserves channel/callback arguments, loads the active
  DMA service table through `0x8017A4EC`, indirectly calls its `+4` exchange
  method, and propagates the backend result. Observed channels 1/2/3/4 map to
  MDEC, GPU, CD-ROM, and SPU; all storage/register/synchronization effects are
  backend-owned and the dispatcher performs no validation.
- `SetIntrMask` owns `0x80162690..0x801626A7`, is a six-instruction leaf with
  nine GPU/DMA-library callers and no interior targets. It loads the active
  I_MASK pointer from `0x8017A4F4` (initialized `0x1F801074`), returns the old
  zero-extended halfword, and writes the low half of the new mask in the return
  delay slot. Callers use zero/save/restore pairs; CPU status and I_STAT are
  unaffected.
- `Strlen` owns `0x8015F5D4..0x8015F5E3` and is the four-word BIOS A0/1B
  trampoline, including an unreachable padding NOP. It has eight direct entry
  callers and no interior targets, preserves the string pointer and original
  `ra`, and propagates the BIOS byte count excluding NUL unchanged. Scanning,
  invalid-pointer behavior, and all reads belong to BIOS.
- `ApplySceneObjectGroupProperty` owns `0x800FA364..0x800FA48F`, has eight
  direct callers and no interior targets. It scans all 0xCC-byte records at
  base `0x8018811C`/count `0x80187CE8`, matches key against signed `+8` or byte
  `+1`, and selector 0/1/2/3 updates linked byte `+4`, randomized halfword
  `+0xA2`, record byte `+2`, or record byte `+3`. Every match is updated; only
  selector 1 advances RNG, once per match.
- `CdIntToPos` owns `0x801649C4..0x80164AC7`, has seven direct callers and no
  interior targets. It adds the 150-sector lead-in, divides signed total by 75
  and 60, packs minute/second/frame components as BCD into output bytes
  `+0/+1/+2`, leaves byte `+3` untouched, and returns the original pointer.
  It is call-free and has no shared or hardware effects.
- `SpawnTrackSectionSurfaceEffects` logically owns
  `0x8014FA4C..0x80150577` despite generated splits at FAE0/FDE0/FF4C/50548.
  Its sole real caller passes a track section and effect flags; it selects a
  flagged geometry face and, over four iterations, derives two randomized
  surface positions/directions and submits eight type-2 effects total. The
  apparent `8002B4F0 -> 8014FDE0` JAL is data decoded as code, not a callable
  interior entry.
- `RegisterSimpleTextureBundleEntries` owns `0x80111E84..0x80111F63` with
  eight callers and no interior targets. It walks the relocated entries using
  signed global count `0x801A4B4C`, converts each through the simpler TIM
  loader at `0x801116A0` with ABR 1, appends nonnull 0x26-byte descriptors to
  the shared texture table/count, and unconditionally frees the bundle backing
  allocation. It is the narrow companion to `RegisterTextureBundleEntries`.
- `LoadSimpleTimTexture` owns `0x801116A0..0x80111AF7` and has the registrar as
  its sole caller. It supports standard indexed TIM modes 0/1, allocates the
  shared 0x26-byte descriptor, derives TPAGE/CLUT/UV state, auto-places allowed
  rectangles, forces STP bit 15 on all 16 colors for 4-bit TIMs, uploads CLUT
  and image data, and returns null for other modes. Unlike
  `LoadFrontendTexture`, it has no special sentinels, variant upload, or pixel
  mirroring.
- `SetDrawMode` is the PsyQ-compatible frameless leaf at
  `0x801601B4..0x80160247`, with 11 callers and no interior entry targets. It
  builds a two-word `DR_MODE`: E1 draw-mode bits from DFE/DTD and masked TPAGE,
  plus an optional E2 texture-window command packed from a RECT (or zero/NOP
  when null). It writes packet state only and has no semantic return.
- `SetPolyF4` is the exact PsyQ-compatible leaf at
  `0x80160254..0x80160267`: it preserves the 24-bit OT link, writes packet
  length 5 to tag byte `+3`, and writes GP0 code `0x28` to command byte `+7`.
  It has 22 callers, all targeting the true entry, and no interior targets;
  callers allocate/advance 24-byte packets and ignore incidental `v0=0x28`.
- `SetVector` is the exact PsyQ-compatible leaf at
  `0x8014B6E0..0x8014B6FB`: its 16 callers all target the true entry, and it
  copies signed 32-bit `x/y/z` arguments to destination offsets `0/4/8` while
  leaving the VECTOR pad at `+0xC` untouched. It has no semantic return.
- `UpdateLoadingMessageCallback` owns `0x8014B6FC..0x8014B82F`. It is installed
  as the callback at `0x8014BA84..8C` rather than directly called: a byte guard
  prevents reentry, a signed-halfword phase advances modulo 48, the shared
  loading string is rendered at phase 0 and rerendered with `_` at phase 24,
  and the callback always services the shared frame/display routine before
  clearing its guard. It accepts no arguments and has no semantic return.
- `ComputeRaceFrameRandom15` is the pure 13-caller leaf at
  `0x80104A1C..0x80104A47`: it hashes the selected-racer seed at `0x80173A8C`
  plus the incrementing race-frame counter at `0x80173AEC` using
  `((int32_t)sum >> 1) ^ (sum << 3)`, then returns the low 15 bits. Unlike
  `NextRandom15`, it mutates no PRNG state; all callers target the true entry.
- `ComputeRaceFrameRandom15OffsetF00` is the independent eight-caller sibling
  at `0x80104A74..0x80104A9F`. It applies the same pure 15-bit mix to
  `selected_racer_seed + race_frame_counter + 0xF00`; it takes no arguments,
  writes no state, and has no interior entry targets.
- `EnterCriticalSection` is the exact 0x10-byte syscall-1 thunk at
  `0x801671D0..0x801671DF`: it clears COP0 Status IEc and returns its previous
  bit value (zero or one). Sixteen unique executable callers produce nineteen
  generated instances because three SIO1 sites occur in two overlay variants.
- `Puts` at `0x801671A8` is a BIOS B0:3F string-output thunk, not a critical-
  section companion. Its three executable instructions end at `0x801671B3`;
  three trailing NOPs fill the 0x18-byte generated owner. Ten unique callers
  have no overlay duplicates or interior targets, and the thunk has no COP0
  effect.
- `DeliverEvent` at `0x80168CF8` is the BIOS B0:07 two-argument event-delivery
  thunk. Three executable instructions end at `0x80168D03`; one trailing NOP
  fills its 0x10-byte generated owner. Eleven unique callers have no overlay
  duplicates or interior targets; matching enabled events are marked ready or
  have their callback invoked according to event mode.
- `Memset` at `0x8015FEF4` is the BIOS A0:2B C memory-fill thunk. Its three
  executable instructions end at `0x8015FEFF`; one trailing NOP fills the
  0x10-byte generated owner. Twelve unique callers have no overlay duplicates
  or interior targets; graphics callers use the general primitive to clear
  structures rather than issue a GPU operation.
- `CdInit` owns exactly `0x80160D28..0x80160DB7`. It tries the lower CD
  interrupt/hardware initializer up to five times; success installs internal
  sync, ready, and data callbacks, clears their companion state, and returns 1.
  Exhaustion prints the retained failure diagnostic and returns 0. Seven unique
  callers have no overlay duplicates or interior targets.
- `EnqueueEffectInstance` is the straight-line leaf at
  `0x80140438..0x801404FF`: it advances the byte-wrapped index of the pool
  referenced by `0x801E1F14`, fills a 48-byte position/vector/parameter record,
  marks it active, and returns 1. Eight unique callers produce nine generated
  instances because `0x8015997C` appears in two overlay variants.
- `CheckAndRecoverGpuTimeout` owns `0x8015F360..0x8015F4A3`. It samples
  `VSync(-1)` and a bounded call-count watchdog, normally returning 0; expiry
  prints GPU diagnostics, masks interrupts, resets tracked GPU/DMA state and
  GP1, restores I_MASK, and returns -1. Its seven GPU-library callers have no
  overlay duplicates or interior targets.
- `CdPosToInt` is the straight-line LibCD leaf at
  `0x80164AC8..0x80164B47`: it BCD-decodes minute/second/frame and returns
  `((minute*60)+second)*75+frame-150`. Seven unique callers have no overlay
  duplicates or interior targets, and the conversion has no state effects.
- `SubmitRumbleDecayRequest` owns `0x80134CE8..0x80134D23` with 12 entry-only
  callers. It replaces rumble-state words `+0x0C/+0x10/+0x14` with Q8 motor
  amplitude, signed priority, and Q8 decay only when both the new amplitude
  and priority are at least the active values. The adjacent updater emits the
  amplitude byte to controller actuator state and applies the decay each tick.
- `TestRacerInputFlag2Or20` is the logical owner at
  `0x80134A44..0x80134AA3`, crossing the artificial generated split at
  `0x80134A58`. It returns false for racer index `-1`; otherwise racer record
  byte `+2 == 1` selects input mask `0x02`, and every other value selects mask
  `0x20`. All eight callers target the true entry; the predicate has no writes.
- `TestRacerInputFlag1Or10` is the paired logical owner at
  `0x801349EC..0x80134A43`, crossing the artificial split at `0x80134A00`.
  Racer index `-1` returns false; otherwise record byte `+2 == 1` selects input
  bit `0x01`, with bit `0x10` selected for every other type. Its seven callers
  all use the true entry, and it performs no writes.
- `SetMatrixTranslation` is the exact four-instruction leaf at
  `0x80135DD8..0x80135DE7`: 11 callers pass a PSX `MATRIX *` plus signed
  translation x/y/z, which are stored at `+0x14/+0x18/+0x1C`. Rotation and
  padding remain untouched; unlike `SetTransformTranslation`, this narrow
  helper does not set the larger transform's `+0x40` state field.
- `GetCurrentArchiveEntryOutputSize` owns `0x8014BC30..0x8014BC6B`, including
  the generated continuation at `0x8014BC4C`. It returns zero for current
  archive index `-1`; otherwise it returns word `+4` (`output_size`) from the
  selected 12-byte record in the table at `0x801E97FC`. Its eight callers all
  target the true entry, and the accessor has no arguments or state changes.
- `EmitDrawModePacketToOrderingTable` owns `0x801107EC..0x80110893` with eight
  entry-only callers. It reserves a 12-byte `DR_MODE` from `gPrimCursor`, uses
  `GetTPage(0, abr, 0, 0)`, builds it with DFE 1 and caller DTD, and prepends
  it to the active ordering-table bucket at signed `depth >> 6`. It returns no
  semantic value; its effects are the packet, cursor, and OT link updates.
- `RestoreCdCallbackConfiguration` owns `0x80138188..0x801381D7` with eight
  entry-only callers. It synchronously issues CD command 1 with null buffers,
  clears the callback slots exchanged by `0x80160D14` and `CdReadyCallback`,
  restores the saved `CdSyncCallback` from `0x801D1B90`, sets the shared
  initialization flag at `0x8017627C`, and returns 1.
- `ExchangeCdInterruptCallback` is the exact five-instruction leaf at
  `0x80160D14..0x80160D27`. It returns the prior callback word at `0x801792C8`
  and replaces it with incoming `a0` in the return delay slot. Its two callers
  pass null; `CdInit` directly installs internal callback `0x80160E44` there.
- `Strncmp` is the canonical BIOS A0:18 trampoline at
  `0x8015CF28..0x8015CF37`: three executable words forward two strings and a
  length bound unchanged and propagate the signed comparison result; its last
  NOP is unreachable padding. Seven unique callers produce eight generated
  instances because `0x800E5E28` is present in two overlay variants.
- `ChangeClearPad` is the canonical BIOS C0:0A trampoline at
  `0x801671C0..0x801671CF`. It forwards the incoming mode and BIOS result
  unchanged; the fourth word is unreachable padding. All seven direct callers
  target the entry, with no overlay duplicates or interior targets.
- `Strcmp` is the canonical BIOS A0:17 trampoline at
  `0x8015F5E4..0x8015F5F3`. It forwards both string pointers and the signed
  BIOS comparison result unchanged; the fourth word is unreachable padding.
  All seven direct callers target the true entry.
- `OpenEvent` and `EnableEvent` are the canonical BIOS B0:08 and B0:0C
  trampolines at `0x8015F5C4..0x8015F5D3` and
  `0x8015F594..0x8015F5A3`. They forward their argument registers and BIOS
  results unchanged. Each has six entry-only callers and one unreachable
  padding NOP.
- `FlushCache` and `SysDeqIntRP` are canonical BIOS A0:44 and C0:03
  trampolines at `0x80168D18..0x80168D27` and
  `0x80168D28..0x80168D37`. Each has six entry-only callers, three executable
  vector-jump words, and one unreachable padding NOP.
- `SetPolyG3` is the exact five-instruction Psy-Q initializer at
  `0x8015FD2C..0x8015FD3F`. It writes packet length 6 to byte `+3` and GPU
  command `0x30` to byte `+7`, leaving all payload/link fields untouched. All
  six callers target the true entry.
- `CopyAndNormalizeControllerPacket` owns `0x80120988..0x80120A87` with seven
  entry callers. It copies/clears eight raw controller bytes, saves their
  original header at destination `+8`, accepts headers `2300/4100/7300`, and
  otherwise forces buttons `FFFF` plus four centered `80` axes. Recognized
  `23FF/41FF/73FF` error headers are additionally canonicalized to `4100`.
- `InitializeMatrixRotationFromFourCoefficients` is the exact straight-line
  leaf at `0x8015FB14..0x8015FB4F`. Its seven unique entry callers supply four
  rotation halfwords; it zeros the other five rotation terms and `+0x12`
  padding, preserves MATRIX translation at `+0x14/+0x18/+0x1C`, and returns
  the input pointer. There are no calls or interior targets.
- `VectorNormal` owns `0x8015FBDC..0x8015FC0B`: it loads a three-word source
  VECTOR, delegates to the GTE Q12 normalization core at `0x8015FC3C`, and
  stores three normalized words to the destination. Nine generated instances
  are six unique entry callers; `0x8015FBF4` is only the post-call continuation,
  with no external interior target.
- `SwapPackedRecordU16FieldsByType` logically owns
  `0x80112088..0x80112123`, including generated branch tails `0x801120B4` and
  `0x801120EC`. Through a pointer slot it swaps U16 fields `+8/+12` for type 2
  or `+4/+8` for type 6 and otherwise does nothing. Eight generated instances
  are four unique entry callers; neither tail has an independent caller.
- `ConsumeOneShotFlag80175F90` logically owns `0x8012C1A4..0x8012C1CB`,
  including its generated zero-return tail at `0x8012C1C4`. It returns zero
  without writing when the flag is clear; otherwise it clears the word and
  returns one. Seven generated instances are four unique entry callers, with
  no interior calls; the sole observed producer sets the word at `0x8012B9E8`.
- `BuildNineByteLookupIndexMap` owns `0x801027CC..0x80102883`. It maps each of
  nine source bytes to its first index in the shared lookup selected by
  `0x80173A30/0x80173A38`, writing zero on failure, then resets selected
  `0x8018AE44` state and records/returns output byte zero. Fourteen generated
  instances are six unique entry callers; all interior dispatch rows are
  loop/post-call continuations.
- `DispatchRacerEffectSpawn` owns `0x8015B3CC..0x8015B443`. It forwards the
  first three spawn arguments and a byte-truncated effect kind to
  `SpawnRacerEffectRecords`, except composite kind 13 expands into sequential
  kinds 14 and 15. Its six unique callers comprise five overlay addresses and
  one main-image address; none targets an interior continuation.
- `ConfigureBumpArena` owns `0x80118E44..0x80118EA3`. Its six resource-loader
  callers configure the base and four-byte-rounded-down capacity of the three
  12-byte descriptors at `0x801D38B8` without resetting their cursors. Indices
  3+ terminate through `Exit(-1)`; the signed check does not reject negatives.
  No caller targets an interior continuation.
- `SetGlobalStereoWidth` owns `0x801222F4..0x80122377`. With audio enabled it
  clamps its byte input to 127 and stores `0x801D6E54`; when the backend gate is
  also enabled it dispatches command 6 with Q8 endpoints `+width/-width`. Ten
  generated instances are six unique callers after overlay/coverage duplicate
  accounting, and no interior block is directly called.
- `WaitForJoyAckAndClearIrq` owns `0x80169D30..0x80169DBF` with seven callers.
  It acknowledges SIO interrupt bit 7 in `I_STAT`, waits for JOY_STAT ACK bit
  7 to clear under the caller-armed Timer 2 deadline, returns zero on timeout,
  or sets JOY_CTRL bit `0x10` and returns one. All callers test the boolean and
  convert failure into their controller/SIO timeout paths.
- `TransformIndexedPairedVertex` owns `0x800F2F48..0x800F2FF3`. It maps vertex
  index `i` to 12-byte pair record `i>>1` at source-owner `+0x18`, reconstructs
  even `{+0,+2,+4}` or odd `{+8,+A,+6}` signed components, transforms them
  through the GTE into the caller's `VECTOR`, and propagates FLAG. There are
  seven unique executable calls but ten generated occurrences from overlap.
- `SwapType4Or8QuadUvPairs` owns `0x80111C0C..0x80111CB7`, crossing the
  generated split at `0x80111C38`. Through a pointer-to-record argument it
  accepts only type bytes 4/8, then swaps UV pairs `0<->1` and `2<->3` over
  record bytes `+8..+F`; other types and all other bytes are unchanged. Four
  executable JAL PCs become eight generated occurrences through mirroring.
- `RecordRacerEliminationCredit` owns `0x80104750..0x80104857` with six
  callers. After a shared suppression gate, it accepts victim destruction
  state 100 for vacant slots or 15 for occupied slots, increments the credited
  racer's word counter at `0x8017C484`, and increments an additional per-racer
  byte in session mode 7 or 6. Incoming `a2` is ignored; no value is returned.
- `ApplyAnalogControllerCalibration` is the true frameless owner of
  `0x801203F0..0x801207A3`; five later generated entries are only internal
  blocks. Its seven entry-only callers select logical pads zero/four. It
  resolves two-port/four-slot multitap packets, calibrates `7300` four-axis or
  `2300` steering/pressure payloads with deadzones, fixed-point scale, clamps,
  and history, then returns the mutated packet pointer or null.
- `DecodeAnimationCommandRecord` owns `0x800E7264..0x800E735B`; generated
  `0x800E7324` is a fall-through split, not a callable, and no overlay duplicate
  or interior target exists. Its six callers pass an animation stream plus a
  signed 16-bit byte offset. It returns the record opcode and reconstructs the
  following 16 unaligned little-endian bytes as four argument words.
- `DoesObjectMaskMatchRacerView` is the exact pure predicate at
  `0x80140E00..0x80140E6F`, with four entry-only renderer callers. It selects
  low view bit 1 for racer type byte `+2 == 1`, otherwise bit 2, ignores the
  opposite bit in the object's mask, and requires exact equality with the
  current scene mask at `0x801762CC` plus the selected bit.
- `AllocateFromThreeBumpArenas` owns `0x80117FD0..0x8011805B`. Its six call
  sites belong to one resource loader. It rounds requests to four bytes,
  first-fits the three base/capacity/cursor descriptors at `0x801D38B8`, stores
  a rounded-size header, advances the selected cursor, returns payload+4, and
  calls BIOS `exit(-1)` if every arena is full.
- `Exit` is the canonical four-word BIOS `B0:38` trampoline at
  `0x8015FD7C..0x8015FD8B`; the last NOP is unreachable padding. Seven
  entry-only callers pass status zero once and `-1` six times. The thunk
  preserves `a0`/caller `ra`, selects service `0x38`, and never returns under
  the BIOS contract; it has no direct memory or hardware effects itself.
- `CdRead` owns the exact LibCD range `0x80160A74..0x80160C0F` with six
  entry-only callers. It waits at most 120 VBlanks for a prior read, publishes
  count/destination/mode, derives 2048/2340/2328-byte sector geometry,
  suppresses and saves CD callbacks, conditionally runs command-9 recovery,
  starts the lower read engine, and returns boolean acceptance; callers then
  poll adjacent read-sync at `0x80160C10`.
- `IsRaceRecordStatusZeroTypeNonzero` is the exact pure leaf at
  `0x80134AD8..0x80134B5F`, with six entry-only callers. It rejects negative
  indices, selects `0x801910E0 + index*0x3A4`, and returns true only for byte
  `+0 == 0` and byte `+3 != 0`; unlike the nearby stricter predicate, values
  other than one also satisfy the second test.
- `EmitTexturedQuadPacket` owns `0x8015B65C..0x8015B7E3`; three calls in one
  UI renderer target the entry, with no interior target or overlay duplicate.
  It reserves and OT-links a 40-byte `POLY_FT4`, writes four XY/UV pairs and
  RGB/CLUT state, selects opcode `2C` or semitransparent `2E`, and on the latter
  path derives ABR-1 TPAGE through canonical `GetTPage`.
- `ApplyRgbToPrimitiveRecordStream` owns `0x800E8350..0x800E8513`; its six
  generated interior entries are the type 1..8 jump-table cases. Four unique
  callers supply an owner (`count +0x12`, stream `+0x1C`) and RGB bytes. It
  overwrites one, three, or four per-vertex RGB triplets at full or unsigned
  half intensity according to the exact variable-size record layout.
- `CdReadSync` owns exactly `0x80160C10..0x80160D13`, with six entry-only
  callers and no generated duplication. It is the synchronizer paired with
  adjacent asynchronous `CdRead`: mode zero waits, nonzero polls, it enforces
  60/1200-vblank recovery deadlines, services optional status output, and
  returns progress/completion or `-1` on timeout/error.
- `InitializeIndexedRacerSpeedState` is the exact leaf at
  `0x800E5480..0x800E54DB`. Its six unique callers select a `0x16C`-byte
  runtime racer; it stores `min(9 * s16[+0x146], floor(5 * u16[+0x14A] / 4))`
  at `+0x132` and clears `+0x14C/+0x12A`. Seven generated calls include one
  duplicate emission of the `0x8014EC5C` instruction.
