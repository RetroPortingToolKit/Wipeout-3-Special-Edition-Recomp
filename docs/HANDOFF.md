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
  unverified and the feature must stay default-off.
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
- With a temporary build-tree-only `auto_skip_fmv=true` probe, the title path
  reached generated game dispatch after PAL x2 and held approximately 99.9–
  105.4 Hz with approximately 44.1 kHz SPU production and zero underruns.
  This validates the title/menu-side workload, but no automated controller
  input has yet selected and completed a race, so race stability and full
  content coverage remain open. The shipped config was restored to
  `auto_skip_fmv=false`.
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
  temporary probe harness is being removed and the production build restored
  with debug tools off, `auto_skip_fmv=false`, netplay off, and rewind off.
- The restored production executable passes the static Release boundary,
  `guest_clock_test`, and the authenticated PAL-title verifier. The build emits
  only existing third-party `stb_image` overflow warnings; no build or test
  failure occurred. PAL100 remains opt-in/default-off.
- Focused FMV evidence is now available for both paths. Stock PAL remains
  PAL x1 at a 20 ms wall-clock cadence; opt-in PAL x2 remains a 10 ms guest
  cadence while the `gl_present_ring` shows movie swaps approximately every
  20 ms (50 Hz). The FMV screenshot is 320x240 and contains no text outside
  the movie bounds, confirming the corrected crop/clipping path. This is a
  cadence/crop pass, not a full content soak.

- PAL100 title timing is now authenticated for the supplied PAL target: the
  title selector/state path at 0x8017BB8C–0x8017BBAC maps selector 2 to
  gSimVblankSkip=1 and PAL-x2 state=1. The opt-in PAL-x2 hook and target-only
  package are present, with `tools/verify_pal100_title.py` covering the exact
  local bytes and hashes. End-to-end PAL-x2 simulation, SPU/CDDA, timeout,
  media-load, and race traces remain open; keep the feature default-off.
- Normal-vs-accelerated memory-card and race-load equivalence is not proven;
  the framework's Fast Loading host-pacing mod remains disabled by default.
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
  yet been captured. Fast Loading therefore remains disabled and equivalence
  is not declared.
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

## Ruled out

- Reusing the existing NTSC-120 package as a PAL-100 patch: PAL sites do not
  have the same instruction layout or timing constants; PAL100 now uses its
  authenticated title-side selector path and a separate PAL-x2 hook.
- Treating HD-pack discovery as proof of replacement: live substitution was
  separately measured and is now confirmed only for the observed 299 entries.
- Repeating an unbounded PAL-100 search without stronger runtime traces: the
  title-side candidate is now authenticated, but PAL x2 remains default-off
  until simulation/SPU/CDDA/media comparisons exist.
