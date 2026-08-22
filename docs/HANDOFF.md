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
  unverified and the feature must stay default-off.

- PAL100 title timing is now authenticated for the supplied PAL target: the
  title selector/state path at 0x8017BB8C–0x8017BBAC maps selector 2 to
  gSimVblankSkip=1 and PAL-x2 state=1. The opt-in PAL-x2 hook and target-only
  package are present, with `tools/verify_pal100_title.py` covering the exact
  local bytes and hashes. End-to-end PAL-x2 simulation, SPU/CDDA, timeout,
  media-load, and race traces remain open; keep the feature default-off.
- Normal-vs-accelerated memory-card and race-load equivalence is not proven;
  acceleration remains disabled by default.
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

1. Run the opt-in PAL100 title path and capture simulation, SPU/CDDA,
   timeout, media-load, and race equivalence traces.
2. Compare normal and accelerated load traces (CD command/sector/DMA,
   memcard writes, state fingerprints, and audio timing) before enabling it.
3. Remove or prove dead all runtime compiler/interpreter paths in the release
   link graph, then rerun the static boundary gate.
4. Validate ultrawide/content coverage and perform a long deterministic soak.

## Ruled out

- Reusing the existing NTSC-120 package as a PAL-100 patch: PAL sites do not
  have the same instruction layout or timing constants; PAL100 now uses its
  authenticated title-side selector path and a separate PAL-x2 hook.
- Treating HD-pack discovery as proof of replacement: live substitution was
  separately measured and is now confirmed only for the observed 299 entries.
- Repeating an unbounded PAL-100 search without stronger runtime traces: the
  title-side candidate is now authenticated, but PAL x2 remains default-off
  until simulation/SPU/CDDA/media comparisons exist.
