# Handoff

## True pal8 media/build lane (2026-08-24)

- A legal original Redump multi-track source was recovered from the local
  archive `D:\Wipeout 3 SE files\WipEout 3 - Special Edition (Europe) (En,Fr,De,Es,It).7z`.
  Its original Track 01 is 94,660,944 bytes with BLAKE3
  `8f7d08844df14b1afe8150fb0e434bf9c4f088834da65c8623025722d56586a5`, which
  is the exact source accepted by Hueponik's `pal8_track01.xdelta3`.
- The patch was applied with the pinned Xdelta 3.2.0 binary. The resulting
  pal8 Track 01 is 94,660,944 bytes, MD5
  `b7bdd4d86ca3b6013341aaca977db715`, SHA-1
  `a93fd7e0121d469fde62547f464e0cb7adf72d5d`, and BLAKE3
  `55f8514a84d67f738919b42ce6f88e9f1016baa5fe728b9ea11acebf7902a635`.
  It is staged separately at `prepared_disc-pal8-20260824` with all 14 tracks;
  the existing plain-PAL `prepared_disc` was not overwritten.
- `build-remaster-pal8-400-20260824.game.toml` is an isolated 8 MiB/PALx2/400%
  profile with rewind off and PGXP/HD off. `prepare_disc.py` accepted the new
  hashes and preserved the 14-track cue/CDDA set; the boot EXE remains
  `PC=0x80162474`.
- The recompiler generated a separate pal8 tree under
  `build-remaster-pal8-400-20260824/generated` (92 full C shards plus dispatch,
  using the current `b24a140a` compiler). Root CMake now accepts
  `PSX_GENERATED_DIR` so this tree can be built without replacing the known-good
  generated tree. The pal8 runtime build used a tracked, conservative
  single/two-worker schedule after prior untracked parallel attempts exhausted
  the Windows pagefile; it produced a directly launched executable without
  compiler errors.
- This lane is an experiment, not yet the replacement baseline. The accepted
  reference remains the current plain-PAL max-mod build with fresh slot 1/2
  states and verified ~99.5-Hz replay cadence.
- The pal8 PGXP executable linked successfully at 83,705,875 bytes. A bounded
  no-launcher retail-BIOS boot was completed under `PID 38072`; that process
  was terminated before returning to the plain-PAL reference. Its startup
  audit resolves the pal8 cue and config, reports
  `tag=cg11_b24a140a_gc3d97ace3_f2`, and settles at 99.43--99.56 guest Hz,
  44.07--44.13 kHz SPU, and zero audio underruns/overflows. This is a boot
  proof only: the pal8 lane has no compatible player save yet and must not be
  judged as a race/replay result until a fresh state is made under this exact
  executable. The process uses isolated writable state in
  `saves-pal8-20260824`; the plain-PAL reference states remain untouched.

## Fresh max-mod race/replay verification (2026-08-24)

- The owner supplied fresh, compatible states made with the max Hueponik mod
  set: `saves/scph1001/state_80162474_slot01.pst` (race start, 2,013,275
  bytes) and `state_80162474_slot02.pst` (post-race replay, 2,020,829 bytes),
  both written at 10:18 and carrying the current B24A codegen/ABI. No new
  saves are required for the current investigation.
- The intended candidate is the 8 MiB/PALx2/400% profile
  `build-remaster-pgxp-off-20260824`, with rewind explicitly disabled. Slot 2
  loads at `PC=0x801722B0`; a 30-second sample stayed at 99.44--99.60 guest
  Hz, SPU 44.08--44.12 kHz, zero audio underruns/overflows, and stable
  ~175--182 ms audio fill. This confirms the user's report that the full race
  and replay now run extremely well; the guest counter is backed by the
  cadence/device telemetry here, not just presentation FPS.
- Exact current-byte overlay fragments were published only through the normal
  compiler transaction (`--force-interior` for observed dispatch
  continuations), with preflight checks and matching B24A codegen: high-RAM
  `A7BEE8BA`, `DDD13CB1`, `BBD9DBAE`, `EC6C0106`, `B0D4C0B1`, `5C13D51C` and
  low-RAM `759DDE91`, `074F91FF`, `31F385C6`, `6B944821`, `72142D48`.
  Each build reported `PSX_SHARD_RESULT failed=0`; no stale shard was copied
  or aliased.
- Native coverage now displaces the prior replay hotspots step-by-step while
  cadence remains full speed. A residual hot interpreter continuation at
  `0x001488E4` is still visible (the capture deliberately lacks a provenance
  root for that interior); it is not currently a performance failure. Do not
  claim that interpreted execution is literally zero: device-touching code and
  unproven observed-only continuations must remain fail-closed until a safe
  coherent capture or decompilation supplies their contracts.
- A bounded capture was taken from the supplied slot 2 replay state at the
  exact high-RAM hot continuation `0x80780E1C`. The normal compiler pipeline
  (`compile_overlays.py --force-interior`, current B24A140A toolchain) produced
  both `00780000_8D944E3C` and its isolated `00780000_696C8F4E` fragment with
  `PSX_SHARD_RESULT ok=2 failed=0`; no stale shard was copied. After a clean
  restart the loader audit rose to `indexed=59`, the former high-RAM hot path
  became native, and the remaining interpreter hotspot moved to low-RAM
  `0x001488FC` while cadence stayed 99.48--99.55 Hz, SPU ~44.10 kHz, and audio
  underruns/overflows remained zero. This is the correct way to reduce
  interpreted work: capture the actual path, compile it, verify loader
  publication, then measure. It does not justify blanket native admission.
- The post-publication slot-2 presentation capture shows the guest `position`,
  `time`, and `lap` HUD text over the replay scene; the gray/missing-UI defect
  is not reproduced in this current plain-PAL run. Keep the older PAL8 gray/FMV
  screenshots as an unresolved separate lane, not as evidence against this
  active candidate.
- A second exact continuation publication moved the active interpreter tail
  from `0x80780E1C` to `0x80780E30`, and the low-RAM publication moved it from
  `0x801488FC` to `0x80148910`; the next sample now reports `0x80780E44`.
  Guest cadence remains 99.48--99.60 Hz with zero audio underruns. This
  adjacent-block progression is expected from isolated CPS continuation
  shards; chasing every 20-byte block is not a safe definition of completion.
  The remaining work is a coherent decompilation/dispatch contract for the
  routine (the owner's separate decompilation effort is directly relevant),
  not a blanket “compile every observed PC” rule.
- The exact low-RAM replay tail `0x80148C88` was then preflighted and published
  through the normal B24A140A compiler transaction (2-entry region plus one
  vetted interior island; `PSX_SHARD_RESULT ok=4 failed=0 skipped=3`). After a
  clean restart from the owner’s slot 2, the low tail became native and the
  interpreter moved to the next high-RAM CPS continuation `0x80780E44`.
  Three five-second windows stayed at 99.49, 99.49, and 99.49 Hz with SPU
  ~44.10 kHz and zero audio errors; the negative miss was cached (`miss_cached=1`,
  `watched_gen=2`). This is a measured improvement, not blanket observed-PC
  admission. Current exact process is PID 37596; logs are
  `build-remaster-pgxp-off-20260824/active-slot2-lowtail.out.log` and `.err.log`.
- The runtime source's local dirty-flow and CPS range logic is unchanged in
  this pass; the missing-tail behavior is being measured from exact user-state
  captures rather than hidden with a broad observed-PC admission rule. The
  current remaining architectural candidate is to preserve dispatch provenance
  for local dirty-flow tail transfers so a coherent CPS owner can be loaded,
  not to promote every observed PC wholesale.
- Old-root compiler jobs under `C:\Users\Shokunin\Documents\ChatGPT\wipeout3se`
  were explicitly terminated before each active-cache compile. Only the exact
  cleanroom candidate process is left running for verification.

## Replay miss-cache and launch provenance audit (2026-08-24)

- The replay slowdown has one verified runtime cause: `memory.c` was notifying
  `overlay_loader` for every manifested-code write, while the lazy negative
  dispatch cache keyed every miss to one global watched-code generation. In the
  replay trace the hot interpreter PC `0x00780984` therefore had
  `miss_cached=0`, `try_calls` grew by roughly 27–31k per five-second sample,
  and guest cadence fell to 43–57 Hz even though `code_gen` stayed at 30 and
  `work guest` stayed near 993 ms/s. This is not a display-counter artifact or
  a proven device-deadline/thread-scheduler defect.
- The cache now records the exact manifest dependency pages and their existing
  page generations; an unrelated overlay write no longer expires a hot miss,
  while a write to any page of a multi-page candidate still does. Entries with
  more than the compact dependency budget conservatively use the aggregate
  watched epoch, so cross-page invalidation cannot leave a stale negative. Loader
  publication, cache rescans, and savestate restore still invalidate through
  their existing global/whole-RAM paths. The runtime guard test covers the
  dependency capture and overflow fallback. Current rebuilt replay logs show
  `miss_cached=1`, stable `watched_gen`, and no continuing manifest-rescan storm
  for the active hot miss.
- A diagnostics-only fix now increments the post-load device-advance
  `adv(n/sum/max)` counters at the actual device chunk boundary. It does not
  change cycle or device timing. Focused cycle, boot-state, and interpreter
  guard tests pass.
- PGXP launch provenance is guarded: the launcher compares canonical and
  sibling `game.toml` hashes and refuses stale mismatches unless an explicit
  profile A/B opts in. CMake stages configs for both normal and PGXP targets and
  accepts `PSX_GAME_CONFIG_SOURCE` so an intentional 400% profile is not
  overwritten by the canonical 900% profile.
- The current max PGXP executable was rebuilt from the patched runtime and
  passes the focused PGXP/HD/presentation/save-state tests. Its slot-3 replay
  state loads successfully at 900%; with the active cache it no longer shows
  the old `0x00780984` miss storm, but guest cadence remains variable (~43–75
  Hz in this stress state). That remaining variability is the next CPU/replay
  investigation target; do not call 400% playable from the old slot-3 state,
  because its boot-state preflight correctly rejects that 900%-timed save.
- A fresh save made under the intended 400% profile is required for a fair
  400% comparison. Do not copy stale overlay shards by hand; use a fresh
  capture and the normal compiler pipeline, then verify the target appears in
  the loader index before measuring performance.
- The fresh player race-start state is the UI slot 1 file
  `saves/scph1001/state_80162474_slot00.pst` (the frontend labels backend slot
  0 as slot 1). A controlled headless replay-capture run from that state first
  measured only 2–5 guest Hz while 67 matching overlay DLLs were mapped, then
  reached 95–100 guest Hz once the cache was warm. A clean relaunch with that
  warmed cache starts near 100 Hz. This proves the apparent 3 FPS stall is a
  cold lazy-overlay loading cliff, not a host-CPU saturation invariant; the
  runtime still needs a bounded prewarm/wait policy so the player does not have
  to drive through the cliff.
- The same player screenshots show a separate unresolved presentation defect:
  menu and in-race 2D/UI primitives are absent or partially drawn, with gray
  uninitialized-looking panels and missing HUD elements. This remains a first-
  class acceptance blocker even when guest cadence is warm; do not treat the
  warm 100-Hz telemetry as a complete product pass.

## Standard-runtime restart baseline (2026-08-23)

- The restart uses `build-psx-standard` with the audited upstream runtime,
  4:3 OpenGL, stock clocks, and every enhancement disabled. A clean boot was
  driven into a live race: the countdown gantry, walls, ships, and track
  geometry were intact, and the race timer advanced ten seconds over a
  ten-second wall interval. The owner independently confirmed it runs
  perfectly. Captures are retained outside the repository as
  `cleanroom-stock-countdown.png` and `cleanroom-stock-race10s.png`.
- PGXP, widescreen, HD textures, fast loading, PAL-x2, and save-state reuse are
  excluded from the baseline. Return them one at a time and pass the same race
  route before enabling the next feature.
- Rejected: the historical Q16 `psx_advance_cycles` charge scaler from commit
  `30652ac3`. An isolated 900% A/B printed the requested value but progressed
  through boot more slowly than stock. It scales the shared cycle coordinate
  and is not evidence of independent CPU retirement. It was reverted; do not
  resurrect it or expose its percentage as a trustworthy counter.
- The next timing implementation must maintain separate CPU-retirement and
  native-device time (or exactly transform every device deadline), then prove
  retired guest work, 100-Hz simulation updates, 44.1-kHz SPU output, native
  CD timing, and the intact countdown/race scene in the same run.
- A second isolated attempt combined the historical CPU scaler with the old
  dynamic PAL CRTC work and deferred activation. It failed sooner: opening
  FMV playback became visibly extremely slow; in the CPU-only leg, memory-card
  and menu loading were also extremely slow and race presentation varied near
  55 frames/s. The owner confirmed each symptom live. All timing changes and
  the temporary package/plugin were removed, and the verified stock Release
  executable was rebuilt and relaunched. Do not combine those historical
  patches again: FMV/native-device scheduling needs a new boundary, not an
  activation-order adjustment.

## Epoch-qualified duplicate resolution through 0x80050DE0 (2026-08-22)

- Dormant binding 207 and live binding 491 share exact destination
  `0x8005ABA0-0x8005ABA4`, image hash `b174f277...15e06c`, and one-entry CFI,
  but not source provenance. Binding 207's authenticated 64 KiB source hash
  `4d473637...b74b7a` is now `8a4d4da0...ebf612`; binding 491's authenticated
  2 MiB source hash is currently `aaea45af...2b42f4f`.
- Exact duplicate replacement now requires a current candidate source, stale
  existing source, identical exact destination range/hash, and identical full
  CFI PC/count set. Equal bytes alone and a still-live competing epoch are
  rejected. No range union or dormant edge is enabled.
- The real probe binds 491 and exact shards through 571, reaching UnknownCode
  `0x80050DE0` at 19,421,870 instructions / 1,837,516 dispatches. CD counters
  remain `0/0/0`. The requested new 100K tranche is not complete (318 retired
  instructions after the resolved handoff); resume with shard 572.
- Receipt: `clean-aot/catalog/evidence/bios-shell-epoch-qualified-8005aba0-80050de0.json`.

## 140,586-instruction shell tranche to 0x8005ABA0 (2026-08-22)

- After the exact-capacity checkpoint, bindings 474-490 advanced normally
  from `0x80047728` through a finite shell loop to `0x8005ABA0`. Relative to
  the requested `0x800502D8` baseline, the tranche retired 140,586 additional
  authenticated instructions (19,421,465 total / 1,837,422 dispatches), well
  beyond the requested 20,000.
- The endpoint is fail-closed: the current executable stamp reports
  `EpochMismatch`; candidate binding 491 for exact bytes `0x240A00B0`
  (`addiu $t2,$zero,0xB0`) and range `0x8005ABA0-0x8005ABA4` is rejected as an
  overlapping duplicate of dormant exact binding 207. No union or dormant edge
  was enabled. Current full-RAM SHA-256 is `aaea45af...2b42f4f`.
- CD reads/writes/raw sectors remain `0/0/0`; no game identity transition.
  Receipt: `clean-aot/catalog/evidence/bios-shell-live-frontiers-80047728-8005aba0.json`.
- Next bounded action is exact duplicate-range epoch reconciliation: refresh or
  deterministically replace the stale binding only after proving identical
  range/hash/instruction-count and rejecting conflicting source identities.

## Exact manifest-capacity checkpoint at 0x80047728 (2026-08-22)

- Exact batched bindings 432-473 advanced from `0x800502D8` through the shell
  decision corridor. Binding 473 initially returned `LoadRejected` because the
  diagnostic probe needed 514 exact slots (76 static plus 438 generated) but
  retained the default 512-slot allocation.
- `advance_live_shell_frontiers.py` now derives the exact allocation from the
  probe's static binding operations and generated manifest lines before every
  build. Capacity remains storage, not authorization; every range/hash/stamp
  and the runtime's strict 4096 upper bound remain unchanged and fail-closed.
- The same shard 473 then bound and advanced to UnknownCode `0x80047728` at
  19,281,031 instructions / 1,790,631 dispatches. CD reads/writes/sectors are
  `0/0/0`. Receipt:
  `clean-aot/catalog/evidence/bios-shell-live-frontiers-800502d8-80047728.json`.
- The requested 20,000-instruction tranche is not complete (current delta 152).
  Resume at binding 474 / PC `0x80047728` with max batch 16.

## DMA6 CHCR deadline catch-up (2026-08-22)

- Cause `0x1C` at authenticated EPC `0x8005027C` was an artificial data-bus
  exception: opcode `0x8D8D0000` is the lawful aligned word read
  `lw $t5,0($t4)` with `$t4=0x1F8010E8`, DMA6 OTC CHCR.
- A 16-instruction generated corridor crossed an internal DMA deadline. The
  CPU MMIO callback converted `MissedDeadline` into a failed memory access and
  therefore `BusErrorData`. CPU access synchronization now services each real
  intervening device deadline in order. The external pacer API remains strict.
- The real probe completes OTC, observes CHCR `0x01000002`, and advances through
  exact bindings 423-431 to UnknownCode `0x800502D8`: 19,280,879 instructions,
  1,790,583 dispatches, and zero CD reads/writes/sectors. Evidence is
  `clean-aot/runtime/evidence/dma6-chcr-deadline-catchup-v1.json`.
- Next action: continue exact authorization at `0x800502D8`; do not revisit the
  old fatal A0 SCC or raise the dispatch bound.

## Native SPU DMA4 request coupling (2026-08-22)

- Whole-machine DMA channel 4 now follows the SPU's delayed SPUSTAT DMA
  read/write request instead of retaining the DMA controller's reset-high
  request. Request-mode transfers wait through SPUCNT application and DMA-read
  warm-up, then run at native device time.
- Boundary, transfer, deassertion, paired SoC/SPU snapshot replay, and GCC plus
  Clang ASan/UBSan proof are recorded in
  `clean-aot/runtime/evidence/spu-dma4-native-request-v1.json`.

## Native XA zigzag resampling (2026-08-22)

- XA 37.8/18.9 kHz audio now uses the documented seven-phase, 29-tap CD-ROM
  zigzag filter instead of linear interpolation while retaining exact 7/6 and
  7/3 frame counts. Mid-phase history is snapshot-safe and fail-closed.
- Independent impulse anchors, chunk/capacity attacks, sector handoff tests,
  and Clang ASan/UBSan results are recorded in
  `clean-aot/media/evidence/xa-zigzag-resampler-v1.json`.

## Native SPU/CDDA deadline ordering (2026-08-22)

- A completed CDDA sector is now published only after SPU catch-up through the
  coincident native deadline, preventing up to 588 frames of retroactive audio.
- Deadline-minus-one, exact-deadline, next-SPU-boundary, irregular-chunk, and
  snapshot/replay checks are recorded in
  `clean-aot/runtime/evidence/spu-cdda-native-event-order-v1.json`; the full
  runtime suite and focused Clang ASan/UBSan test pass.

## Live a0==0 continuation through frontier 0x8005161C (2026-08-22)

- Live stack/CFI execution followed 103 exact fail-closed frontiers from
  `0x80050A7C` through the current `a0==0` initializer path. The requested
  100-frontier tranche starts at `0x80050A88` and ends at UnknownCode
  `0x8005161C`; no edge was forced from the static `46da91b` route contract.
- Two repeated probes are byte-identical (SHA-256 `d7f5afaa...1801`) at
  19,272,155 instructions / 1,787,780 dispatches. Terminal `$ra=0x80047450`,
  `$sp=0x801FFD60`, Cause is zero, and CD MMIO reads/writes/raw sectors remain
  `0/0/0`. The later `a0==1` invocation and `0x80030120` have not yet occurred.
- Canonical receipt: `bios-shell-live-frontiers-8005161c.json` (SHA-256
  `784129ee...79fc`). It retains all 103 exact source/variant/image/manifest
  identities. Generated C++ remains ignored scratch;
  `advance_live_shell_frontiers.py` deterministically regenerates the batch.

## Root-counter target/overflow reset dwell (2026-08-22)

- Target reset now holds zero for two selected-source ticks; ordinary `FFFFh`
  overflow holds zero for one. Dwell is exact across bulk/chunked native time,
  serialized and range-validated in snapshots, and never CPU-retirement scaled.
- Strict/sanitizer verification and remaining unwaived write/pulse delays are
  recorded in `clean-aot/soc/evidence/root-counter-reset-dwell-v1.json`; see
  `docs/ROOT_COUNTER_RESET_DWELL.md`.

## Atomic installed-runtime acceptance (2026-08-22)

- Installer acceptance now executes the same atomic install/rollback primitive
  used by `Wipeout3SE-Setup.exe`, targeting a separate Unicode installation
  directory after relocating the installer archive. It independently reaudits
  the installed bytes and rejects setup/compiler payload leakage.
- The install primitive now lives in the installer policy module, so setup UX,
  acceptance, and rollback tests exercise one implementation. Release readiness
  requires this proof. The matrix remains honestly 12/15 with only the three
  game/media/behavior execution failures.

## CD-ROM Reset native reboot timing (2026-08-22)

- Command `0x1C` now retains its INT3-only response while holding the command
  interface busy for exactly 4,233,600 native cycles (1/8 second). Commands are
  rejected through deadline-minus-one and accepted exactly at the deadline;
  no synthetic completion IRQ is emitted and CPU retirement scaling is absent.
- Strict and sanitizer evidence is recorded in
  `clean-aot/cdrom/evidence/reset-reboot-timing-v1.json`; analysis is in
  `docs/CDROM_RESET_REBOOT_TIMING.md`.

## CD-ROM runtime lid accuracy (2026-08-22)

- CD ShellOpen was defined but unreachable. `Cdrom::set_shell_open` now models
  runtime lid state independently from mounted media identity. Opening stops
  read/CDDA/seek work, cancels media phases, clears sector and DMA buffers,
  drops motor state, and invalidates cached locations; Getstat exposes bit
  `0x10`, while disc-dependent commands use their normal INT5 error path.
- Closing the lid preserves the same caller-owned source/layout. An adversarial
  active-ReadN transition test passes strict GCC 16.1 and Clang 22.1.8
  ASan/UBSan. See `docs/CDROM_RUNTIME_LID_ACCURACY.md` and the deterministic
  `clean-aot/cdrom/evidence/runtime-lid-accuracy-v1.json` receipt.

## A0 service 0x40 exact fatal-cycle analysis (2026-08-22)

- Exact BIOS and RAM bytes prove service `0x40` is a non-returning fatal sink:
  `BFC0D8E0 -> A0 -> 5C4 -> table[0x300] -> BFC0D8E0`. It has zero stack delta,
  preserves `sp`/`ra`/arguments/saved registers, and has no return contract.
- The caller is the general-exception handler fatal arm at `0x1B08`. The live
  shell performed a legal GPUREAD synchronization load at `0x80051054`; the
  traced baseline runtime incorrectly rejected GPUREAD when its transfer latch
  was not ready, causing `BusErrorData` and the fatal path. Continuous calls are
  one intentional error cycle, not finite initialization.
- A concurrent runtime owner already has the matching GPUREAD correction in the
  worktree. The corrected bounded probe proves the fatal cycle disappears,
  Cause remains zero, and the live continuation returns through
  `$ra=0x8004ED94` to the next exact fail-closed frontier `0x80050A7C` after
  19,271,351 instructions / 1,787,466 dispatches. CD MMIO and raw-sector counts
  remain zero. See `docs/A0_SERVICE40_ANALYSIS.md`,
  `bios-a0-service40-audit.json`, and
  `bios-shell-a0-service40-gpuread-fix-80050a7c.json`.

## Independent interrupt audit at PC 0x000005C4 (2026-08-22)

- The committed route terminal was mislabeled as a low-RAM interrupt handler.
  Exact verified BIOS and terminal RAM bytes prove the path is A0 service 0x40:
  `0xBFC0D8E0 -> 0xA0 -> 0x5C4`, where the dispatcher reads table entry
  `0x300 = 0xBFC0D8E0`. The actual general-exception path is independently
  `0x80 -> 0xC80` and was not traversed by the terminal transition.
- Exact CRTC VBlank is edge-latched in I_STAT; I_STAT is zero-to-clear; only
  still-active DMA/CD/GPU/SIO/SPU levels are resampled; `(I_STAT & I_MASK)`
  drives COP0 IP2. Generated exception entry disables current IE by pushing the
  low Status mode stack, and RFE restores it. These semantics do not establish
  a storm or missing return here.
- The route receipt contains no I_STAT, I_MASK, COP0 Status/Cause/EPC, stack, or
  vector-visit telemetry, so no source attribution is honest. No runtime/SoC
  defect was independently proven and production/runtime was unchanged.
  Canonical receipt: `bios-shell-interrupt-audit-000005c4.json` (SHA-256
  `4780941e...1999`); full report: `docs/SHELL_INTERRUPT_AUDIT_000005C4.md`.

## Goal-directed shell disc route at frontier 239 (2026-08-22)

- Authenticated resident bytes and the committed frontier-239 receipt now bind
  a deterministic pre-disc route from shell entry `0x80030000`, through the
  live `0x80050F8C` initializer/callback corridor, to the first disc-status call
  at `0x80030120 -> 0x800359C4`. The receipt verifies 24 direct calls, eight
  branch prerequisites, three bounded loops, all named delay slots, and three
  finite return contracts without executing guest code or changing boot runtime.
- The SPU path retains its real finite work: 256 source-buffer iterations and
  sixteen 4-KiB transfers (64 KiB total). The pre-query `0x80035F50` result
  only changes state and both signs rejoin before the goal call. No disc branch
  and no independently proven hardware defect occurs in this slice.
- Frontier 239 carries no stack/register payload. The return from
  `0x80051300` is narrowed to eight byte-proven direct-JAL continuations but
  cannot honestly select one. Resume the authenticated machine stack to choose
  that member; do not synthesize `$ra`. Canonical receipt:
  `clean-aot/catalog/evidence/bios-shell-disc-route-frontier239.json` (SHA-256
  `d9b7d422...52c0`); analysis:
  `docs/SHELL_DISC_ROUTE_FRONTIER239.md`.
- The route has now been compiled into 17 exact apertures (636 bytes, 38
  roots, 49 entries, 18 shards). A whole-resident-image binding correctly
  rejected because nine unrelated mutable data bytes differ; 15 exact
  apertures bind and the two loop apertures reuse existing exact static shards.
  Live execution at `0x80051018` proves the current invocation has `a0==0` and
  takes `0x80050F94`, not the later route's `a0==1` edge. It therefore never
  reaches the eight-way return at `0x800513C4`; no return target was guessed.
  The exact continuation reaches continuously executable low-RAM interrupt
  handler `0x000005C4` at the three-million-dispatch bound with zero CD MMIO.
  Diagnose the pending interrupt/vector-table source before resuming the later
  goal-route invocation. Receipt:
  `clean-aot/catalog/evidence/bios-shell-disc-route-execution-000005c4.json`.

## Dual-card product evidence rebinding (2026-08-22)

- The genuine dual-card `Wipeout3SE.exe` is packaged at executable SHA-256
  `2dfcca55...` and release-manifest SHA-256 `018f4712...`. Settings, persistence,
  all four 4K/ultrawide soak profiles, graphics matrix, Windows dependency/link
  audit, installer acceptance, and readiness receipts are bound to that identity.
- Installer acceptance now relocates the complete installer under a Unicode path,
  independently validates its 20-file inventory and nested compiler-free runtime,
  and proves the two authenticated game variants retain 135,829 instructions,
  290 regions, zero exclusions, and zero unresolved CFI sites.
- All product-side readiness gates are restored: 12/15 pass. The only failures
  remain direct game-instruction execution, 64-route behavioral execution, and
  authenticated media execution. Game-ready remains false.

## Native runtime settings UX (2026-08-22)

- `Wipeout3SE.exe` now opens a compiler-free Win32 settings window for missing
  first-run configuration or `--settings-ui`. Native tab controls cover
  display/4K/ultrawide, graphics enhancements, audio device/latency, controller
  deadzone/rumble, card slots, reset defaults, save, and cancel.
- Native controls and `--settings-ui-action` automation share one strict schema
  validator and durable atomic writer. Unicode save, byte-identical cancel,
  invalid rejection, and reset recovery pass in the genuine installed package.
- Canonical settings UI acceptance is bound to package manifest `849a197b...`
  and executable `00ad0cfd...`; it remains explicitly not game-ready.

## Memory-card persistence and recovery acceptance (2026-08-22)

- `Wipeout3SE.exe` now loads or recovers both durable 128-KiB card images,
  attaches them to SIO0 ports one and two, and atomically flushes each committed
  sector with two-generation backup rotation. Setup now rejects size-correct
  cards with malformed PS1 header/directory checksums as well as bad sizes.
- Product acceptance binds native proof and required persistence symbols to the
  installed package/link map, preserves
  both exact card hashes through real setup upgrade/rollback functions, checks
  all 64 scripted scenarios remain at 100 Hz, and rejects save-state material.
  This is memory-card persistence only; no emulator save states were added.
- Canonical acceptance is bound to package manifest `018f4712...`, executable
  `2dfcca55...`, and link map `9d96abea...`. The persistence release gate passes.
  Overall readiness remains fail-closed at 9/15 because the new executable also
  invalidates older setup/graphics/soak package bindings, alongside the three
  genuine game/media/behavior blockers.

## Behavioral route verification harness (2026-08-22)

- The product tooling now derives a deterministic 64-route scripted-input plan
  at 100 Hz for all required menu/mode/track/team/ship/FMV/audio routes.
- `behavior-run` invokes only the validated installed `Wipeout3SE.exe` and will
  publish eligible evidence only after authenticated game boot and exact
  package/executable/media/scenario identity checks. Every completed route must
  include remeasured static trace, frame, audio, and save captures.
- A current real invocation at `modes:single-race` exits 2 with no receipt:
  `installed product produced no behavioral proof`. This is the intended dormant
  state at the BIOS frontier. The coverage ledger remains 0/64 and fail closed.

## Split content coverage gate (2026-08-21)

- Product content readiness now has three independently hash-bound contracts:
  verified external-media availability, static executable/overlay generation,
  and clean installed-product behavioral execution. The first two pass for
  5,179 data payloads, 14 disc tracks, two authenticated executable identities,
  and 135,829 generated instructions with zero unresolved CFI.
- Behavioral evidence remains fail closed: all 64 policy routes are currently
  unexecuted (6 modes, 16 tracks, 8 teams, 8 ships, 8 menus, 3 FMV routes, and
  15 audio routes). Quarantined or non-product evidence is ineligible.
- `validate-release` is deterministic at 11 pass / 3 fail. Remaining failures
  are direct BIOS-to-game execution, behavioral-route execution, and
  authenticated media execution. Do not promote game readiness until those
  real installed-product paths execute.

## Same-epoch overlap registry and live shell loop (2026-08-21)

- Copied-RAM registries now retain independent exact range/hash/stamp contracts
  when generated bindings overlap. Dispatch considers only bindings that both
  cover the current PC and contain that exact entry, then selects the narrowest
  range deterministically. Equal-width ties use start address and identity.
- Exact duplicate contracts and conflicting identity/hash reuse reject. Every
  covering contract is mutation-validated before dispatch, invalidated slots
  are reclaimed, and the bounded metadata capacity is 256 exact bindings. No
  page-wide byte union or fallback was introduced. Legacy and packed generated
  tables share the same checked entry-resolution path.
- The real probe advances through 16 additional exact shell/handler frontiers:
  `0x80054C90`, `0x8005AA90`, `0xC80`, `0x8005AA98`, `0x8005A9D0`,
  `0x1D8C`, `0x1D00`, `0x8005A9E0`, `0x1F10`, `0x8005AA10`,
  `0x8005AA18`, `0x800522C0`, `0x800560C0`, `0x80056324`,
  `0x8003FFB0`, and `0x80054428`.
- From the final tranche, execution remains live through the bounded
  one-million-dispatch probe and stops at `0x800545B8` with 9,473,267
  instructions, 1,989,635 reads, 1,489,510 writes, and device totals
  2,331/22,951. Two probes agree exactly. Instrumented classification found no
  CD MMIO, GetID, ReadN, or raw-sector activity. This is a materially different
  live-loop/timing blocker, not an authorization frontier. Canonical evidence:
  `clean-aot/catalog/evidence/bios-shell-cd-execution-09473267.json`.

## SPU shell initialization frontier (2026-08-21)

- Four newly authenticated copied-shell tranches advance the real path from
  `0x8003FDB0` through `0x8003FCC8`, `0x80054060`, and `0x800544E0`.
- At `0x80054384`, the shell legally reads SPU KON (`0x1F801D88`) before
  read/modify/write initialization. Treating the write-only latch read as an
  unmapped MMIO fault incorrectly raised Cause `0x1C`. KON/KOFF now expose the
  hardware-visible zero value on reads; an adversarial test covers both halves.
- The corrected probe reaches UnknownCode `0x80054710` after 2,767,140
  instructions / 462,105 dispatches, 455,346 reads, 459,322 writes, 7,680
  isolated writes, and device totals 60/259. Repeated probes agree. There is
  still no CD MMIO, GetID, ReadN, or raw-sector activity.
- The next exact range overlaps an already bound tranche: the current compiler
  emits an entry for `0x80054710`, but runtime lookup retains the earlier
  bounded region, whose live graph did not include this indirect return entry.
  Reconcile overlapping same-epoch registries without widening authorization;
  do not infer dormant entries. Canonical evidence is
  `clean-aot/catalog/evidence/bios-shell-cd-execution-02767140.json`.

## BIOS shell/CD diagnosis (2026-08-21)

- The syscall exception now returns correctly. Exact live authentication follows
  vector `0x80000080`, handler `0xC80`, callback `0x1A00`, restore path
  `0xF40`, and resumes at EPC+4 `0xBFC0D968` while retaining Cause `0x20` and
  EPC `0xBFC0D964`. A stale packaged compiler had falsely emitted Reserved
  Instruction for SLTIU at `0x1A38`; the current compiler removes that false
  A0/0x40 path.
- Subsequent exact shell callbacks advance deterministically to copied-shell PC
  `0x8003FDB0`: 2,725,528 instructions / 458,801 dispatches, 445,880 reads,
  452,860 writes, 7,680 isolated writes, and unchanged device totals (2 reads,
  80 writes). Two repeated probes agree. No CD MMIO, GetID, ReadN, DMA transfer,
  or raw-sector read has occurred, so this remains a pre-CD shell frontier.
- Canonical execution evidence is
  `clean-aot/catalog/evidence/bios-shell-cd-execution-02725528.json`.

- The verified European Track 01 sector 4 contains the authentic spaced Sony
  Europe license banner. `DiscSource` previously discarded that identity and
  left GetID neutral; it now derives `disc_type=0x20` and `SCEE` strictly from
  those mounted bytes. Unknown media remains neutral and ambiguous banners
  fail closed.
- Zero reads had an additional control-flow cause: `0x80030000` is copied BIOS
  shell code (byte-identical to BIOS offset `0x18000`), not a game boundary.
  Executing only newly authenticated live shell regions reaches
  `0x80030000 -> 0x8005A8D0 -> 0xBFC072B8`, then the exception vector at
  `0x80000080` with Cause `0x20` and EPC `0xBFC0D964`. CD MMIO remains empty;
  no sector-read claim is made. The deterministic diagnosis is
  `clean-aot/catalog/evidence/bios-shell-cd-diagnosis.json`.
- Next: reauthenticate the exception-vector epoch reached by shell syscall,
  return through the exact shell callback, and continue live shell regions to
  the first GetID/ReadN. Do not bind the game registry at `0x80030000`.

## Authenticated game-bind rejection (2026-08-21)

- The real transition probe now mounts the verified BIN/CUE and links the
  latest precompiled main-game shard/CFI registry. The BIOS nevertheless reads
  zero raw sectors and reaches `0x80030000` with the same shell RAM epoch.
- The complete resident main load contract `[0x80010000,0x8017C000)` hashes to
  `eab668f3b877ca88b76acb2ab595009ebcdb88c8ff7d8cfe295c05ff4be086ba`,
  not authenticated main identity `d483d384...`; `0x80030000` is also not a
  generated registry root. Binding therefore rejects fail-closed and executes
  zero game instructions. No shell bytes were authorized.
- Canonical evidence is `bios-to-game-execution-00000000.json`. The next task
  is the CD boot/device path that currently performs no sector read, not game
  shard expansion or relaxed resident identity matching.

## Real BIOS-to-game transition (2026-08-21)

- Authenticating only copied-RAM `[0x1508,0x15D8)` in epoch `778e17e8...`
  advances the real SCPH1001 BIOS directly to game-space PC `0x80030000`.
  No game bytes were authorized by this tranche; `0x80030000` remains the
  honest fail-closed boundary for the authenticated game binding.
- Two probes agree exactly: 2,695,618 BIOS instructions / 452,182 dispatches,
  441,893 reads, 451,154 writes, 7,680 isolated writes, two device reads, and
  80 device writes. The resulting RAM epoch is
  `7301264d93bfaa6235a8655610d2674746a44d50b82645569f1e2f9b0e99a3b6`.
- Canonical evidence is `bios-frontier-02695618.json`. Continue by proving the
  loaded game identity at `0x80030000` against finalized game-shard authority,
  not by extending BIOS manifests or adding fallback.

## Authenticated BIOS callback tranche (2026-08-21)

- From `0x1B20` / RAM epoch `29d475f7...`, the corrected compiler advanced
  through five exact live frontiers: copied-RAM `[0x1B20,0x1B44)`, callback
  `[0x1420,0x1444)`, copied-RAM `[0x2724,0x278C)`, and immutable BIOS
  continuations `0xBFC04678` and `0xBFC0472C` inside the existing
  `[0xBFC04610,0xBFC04850)` range.
- `0xBFC0472C` was verified as the saved caller return restored by the
  `0xBFC04678` continuation, not a sequential-JR defect. Correct pre-delay
  predicate/target sampling and the corrected retail RAM aperture remain in
  force; no semantic or hardware workaround was added.
- Repeated probes stop identically at UnknownCode `0x00001508`: 138,122
  instructions, 26,099 dispatches, 15,905 reads, 23,822 writes, 6,400
  isolated writes, one device read, and 62 device writes. Final RAM epoch is
  `778e17e876cd4a9d39482302fb32ef36c80a5bcbfaa1029c06af95c61a2b7a8e`;
  canonical evidence is `bios-frontier-00138122.json`.

## BIOS allocator semantic fix (2026-08-21)

- The `0x1274` allocator runaway was a compiler delay-slot ordering defect.
  The live sequence computes `r1=(33 < 8193)` before `BEQ`, then its delay slot
  computes the unrelated `r1=(8193 < 32)`. Generated code incorrectly tested
  the second value. Conditional predicates and indirect targets are now
  captured before their delay slots, with adversarial emitter coverage.
- Early and 100,000th-hit state, allocator RAM words, recent PCs, and stable
  mutation/device counters are pinned in
  `clean-aot/catalog/evidence/bios-allocator-loop-diagnosis.json`.
- Two corrected real probes agree exactly: 121,186 instructions / 22,272
  dispatches, final RAM epoch `29d475f7311496a0337b75f0e7842703b62c25db595700c9a4b3abe49b1d537b`,
  and honest UnknownCode `0x00001B20`. See `bios-frontier-00121186.json`.

## Two-stage authenticated game-shard integration (2026-08-21)

- Installed direct launch now selects copied epochs by the current stopped PC
  and retries only hash-authenticated overlapping candidates. This removes the
  stale-registry-order rejection and matches the canonical 2,695,618-instruction,
  452,182-dispatch BIOS frontier at `0x80030000`. The subsequent BIOS-only smoke
  rejection is the expected resident-game identity boundary, not game execution.

- First-run setup can now consume the hash-pinned game-shard authority and its
  two closed manifests, derive the exact main and route-mutated NFE images from
  verified user media, and emit native sources without retaining source media.
- The generated product registry binds game identities/accessors one-to-one and
  links their sources into `Wipeout3SE.exe`; the installed runtime remains
  compiler/setup-free. Resident game dispatch hashes the complete RAM image and
  rejects identity mutation or PCs outside closed shard coverage before dispatch.
- The stale 727-unit snapshot is superseded by the current authenticated
  83,244-instruction/583-region receipt: main contributes 75,217 instructions
  in 450 regions and route-mutated NFE contributes 8,027 in 133. Setup,
  registry, and installer acceptance now re-derive these exact entry totals and
  identities from the packaged receipt rather than trusting aggregate metadata.
- Synthetic BIOS-to-game transition and identity-mutation tests pass. A genuine
  direct product launch still stops in the authenticated BIOS frontier; this is
  not evidence of real game execution and game-ready remains false.

## BIOS allocator-runaway checkpoint (2026-08-21)

- From UnknownCode `0x1174` / RAM epoch `6c6da3c3...`, exact live-region
  authentication advanced through `[0x1174,0x1408)`, callback
  `[0x1030,0x10B8)`, and the observed branch continuation
  `[0x10E4,0x113C)`. The adjacent entry at `0x10B8` remains unauthorized.
- The path no longer reaches UnknownCode. Two repeated bounded probes remain
  authenticated for the full 1,000,000-dispatch ceiling and stop at `0x1274`
  inside the allocator loop: 2,320,994 instructions, 13,663 reads, 22,646
  writes, 6,200 isolated writes, one device read, and 62 device writes.
- This is a materially different blocker, not an authorization frontier. The
  final RAM epoch is `a968043add6eed35610a894bbef805faeaafe205981e3402b7f9ec18953ca9db`;
  canonical evidence is
  `clean-aot/catalog/evidence/bios-frontier-02320994.json`. Diagnose the free
  list/state feeding the live `0x1274` loop before extending any code region or
  dispatch ceiling.

## Authenticated BIOS frontier checkpoint (2026-08-21)

- The real SCPH1001 BIOS path advanced through five additional exact,
  fail-closed frontiers from immutable BIOS PC `0xBFC00DA0`: copied-RAM
  `[0x406C,0x4134)`, `[0x2C94,0x2E00)`, `[0x3E80,0x3F04)`, and, after a
  measured RAM mutation, `[0x113C,0x1174)`.
- The repeated probe is deterministic at 120,902 instructions / 22,208 native
  dispatches, with 13,654 reads, 22,634 writes, 6,200 cache-isolated writes,
  one device read, and 62 device writes. It stops honestly at UnknownCode
  `0x00001174`; COP0 status/cause/EPC/bad-vaddr remain zero.
- The final low-RAM epoch is
  `6c6da3c3b6f7fcb315def23609b5118a25f94ee12e68ed75f0b9f34d43bd980a`.
  Reauthenticate the bytes at `0x1174` from this epoch before continuing; do
  not reuse the immediately preceding `34fa157f...` image.
- Canonical evidence is
  `clean-aot/catalog/evidence/bios-frontier-00120902.json`. No decoder,
  interpreter, dormant edge, or unknown-PC fallback was added.

## Native production presentation checkpoint (2026-08-21)

- The Windows SDL3/Vulkan presenter is now a complete live host backend rather
  than a shader-validation placeholder. It uploads raster scanout through a
  reusable staging texture, samples it into the swapchain viewport, owns exact
  44.1-kHz audio submission, and retains replacement RGBA textures.
- App graphics policy maps deterministically to 3840x2160 exact-4:3 defaults or
  explicit 16:9/21:9/32:9/custom output. The headless backend implements the
  same replacement upload contract with pinned digest `f2a57b68e1fd4d73`.
- The pinned DXC shader rebuild matches the checked-in SPIR-V header. Strict
  native GCC tests pass 11/11 for the app graph and 5/5 for the host graph,
  including a real Windows Vulkan frame/replacement upload runtime test.

## Live graphics integration checkpoint (2026-08-21)

- The application has an opt-in production scanout mode which drains the
  runtime GPU queue into application-owned raster VRAM, plans scanout from live
  GP1 display registers, decodes RGB555/packed-24-bit output, and submits it to
  the host within the same update.
- Runtime configuration supports 4K and explicit generic/custom wide modes.
  Exact 4:3 remains the default and has pinned end-to-end visual digest
  `a0c4b5214903b3dd` from native GP0 fill through host consumption.
- Replacement loading verifies generation-safe identity, confined canonical
  paths, SHA-256, declared dimensions, native WIC PNG-to-RGBA8 decode, and an
  explicit upload sink. Texture/font key derivation remains title-owned; no
  title hashes were added to generic code.
- Strict GCC application-graph tests pass 10/10. Focused Clang 22 ASan/UBSan
  tests pass 5/5 across app, GPU, render, and raster.

## Current position

- Cross-evidenced executable authorization v2 is ready in
  `clean-aot/catalog/evidence/executable-authorization.json` as a deterministic,
  fail-closed receipt (SHA-256
  `49f9ecb8f966849786b5c29370a1341018d0ec2c0bcf4af2d79787771636f83a`).
  Its delta receipt (SHA-256
  `941a5e8b2ab0a45b2821cbd9729c40503c78d74ba42f3b025c913e98a2f03dd1`)
  proves 1,166 formerly rejected NFE-to-main direct calls and adds 2,202
  authorized main-image instructions. It now authorizes 101,033 main-image,
  31,719 NFE, and 3,013 exact verified-BIOS direct-frontier instructions. It
  remains non-selectable: 71 game-image direct edges, 142 game-image indirect
  sites, and 27 BIOS indirect sites remain rejected. Exact categories and all
  270 main, 18 NFE, and 56 BIOS unknown ranges are in
  `docs/EXECUTABLE_AUTHORIZATION.md` and the JSON receipt.

- The original dirty checkout remains preserved at
  `C:\Users\Shokunin\Documents\ChatGPT\wipeout3se`.
- The canonical cleanroom is
  `C:\Users\Shokunin\Documents\ChatGPT\wipeout3se-cleanroom` on
  `codex/remaster-cleanroom`, committed at `cb89d3a` with the reviewed device,
  runtime, host, media, raster, and integration tranches still uncommitted in
  the shared worktree. Do not discard or overwrite this dirty state.
- `psxrecomp@d56bd024` is now classified as a quarantined, non-shipping route
  runner. Its available history is title-tainted back to the local root and is
  not a clean framework base.
- A generic CPS fallthrough defect was independently reproduced and fixed in
  the legacy runner: a reachable unnamed straight-line successor now publishes
  the architectural next PC instead of surfacing a false `PC=0` exit.
- The deterministic route
  `tests/routes/intro-to-first-race.json` completed all 18 steps / 3,117 guest
  frames and reached a live FEISAR race on Porto Kora. The ignored evidence is:
  - `build-clean-baseline/evidence/intro-to-first-race-receipt.json`
  - `build-clean-baseline/evidence/bridge-route-end.png`
  - `build-clean-baseline/evidence/legacy-race-baseline.json`
- The legacy race baseline proves this is not the product architecture:
  approximately 290 million interpreted instructions, 225 million interpreter
  dispatch fallbacks, zero native overlay dispatches, and a 21.4 ms scene-GPU
  maximum while still paced at 59.94 Hz.
- Sol Ultra selected a dual-lane strangler replacement. New original work lives
  under `clean-aot/`; the legacy lane remains only until each boot/race milestone
  has a deterministic replacement.
- The prepared PS-X EXE is now independently inventoried as one immutable image:
  load range `[0x80010000, 0x8017C000)`, entry `0x80162474`, text SHA-256
  `d483d38423965f7995e3d13fc919c887cf63c3f7f2b9d618859c80771488b0c3`.
  The full local file SHA-256 is
  `7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac`.
- The first large PBP compressed-entry hypothesis was falsified. Entry 10 of
  the sampled race container decodes exactly from 109,523 to 207,684 bytes,
  but starts with named tunnel/wall scene records and geometry-like data. It is
  an asset payload, not evidence of an executable overlay.
- A hash-pinned Ghidra 12.1.3 headless pass over the exact text image produced
  1,208 untrusted candidate function roots, 139,387 candidate instructions and
  259 coalesced body ranges covering 557,548 bytes (37.4% of the image). These
  are discovery hints only. Probing the current compiler with one image-wide
  region correctly exposed why bounded code/data regions are mandatory: linear
  fallthrough eventually decodes asset/data words as control transfers.
- All 88 on-disc PBP race containers were extracted into ignored evidence and
  verified without retaining decoded bytes in Git: 5,172 entries total, 3,086
  exact bounded-LZSS payloads and 2,086 exact stored payloads.
- The exact deterministic race was replayed once with the quarantined runner's
  capture path explicitly armed. The ignored v2 manifest SHA-256 is
  `ae880d9e52b6aaddf97295d2f3db735d25190264d05adda047fbc8ab5f6a2a32`.
  Independent normalization found six non-overlapping RAM regions, 118,808
  captured bytes, 12,199 exact observed PCs, and 10,983 candidate entry roots.
  The receipt remains discovery-only and explicitly does not prove semantic or
  executable coverage. Its ignored normalized receipt SHA-256 is
  `53c475bccb58e888a7d0b2ba521d64eb345959d08c6a8958a741d78a1d2334c6`.
- The dynamic race bytes are no longer an undifferentiated overlay hypothesis.
  Exact bounded expansion of all 88 PBP containers plus `NFE.PB` proves that
  four high-RAM capture regions are loaded from `NFE.PB` entry 5. Three regions
  are byte-identical source slices. The fourth compares 52,940 authenticated
  source bytes and differs at only four aligned words; all four immediate values
  are exactly doubled, and two of those words were observed executing on this
  route. This establishes a finite runtime variant but does not yet assign a
  timing semantic to the edits. The complete logical-source match receipt
  SHA-256 is
  `ad65b51fa8921cc336b2cc0260a8dbbd2361edf0bf482c71f5ac6c77aca19b76`.
- The first low-RAM capture is now pinned to the user's exact retail SCPH1001
  BIOS slice at file offset `0x13B00`. Across 12,292 captured bytes, 306 bytes in
  87 aligned words differ at runtime; 10 changed words were observed executing.
  The ignored exact delta receipt SHA-256 is
  `18122448535f526b1b12b28d34a8a29b27c7c640bf2e31130035f072151929b8`.
  The second low-RAM region is zero except for a tail copied by the game. Its
  complete 116-byte live slice at `[0x8000DF7C,0x8000DFF0)` is byte-identical
  to authenticated patched-EXE text at
  `[0x801673D4,0x80167448)`; all 26 observed PCs lie inside that exact copy.
  The bytes independently match the documented PlayStation card-specific-delay
  and early-card-IRQ BIOS patch shapes, but the catalog needs only the measured
  source identity and relocation. The ignored exact-copy receipt SHA-256 is
  `da73bfd703c16824ada60e42e3b0e6ec2be9f1019a3eca387e0899020cd05263`.
- A fresh unseeded Ghidra 12.1.3 pass over exact `NFE.PB` entry 5, loaded at the
  source-to-RAM mapping base `0x800BF800`, reports 109 candidate functions and
  33,604 candidate instructions. Normalization coalesces 134,416 of the
  136,908 payload bytes into three candidate body ranges, with the main range
  `[0x800BF9B0,0x800E041C)`. The ignored normalized hint receipt SHA-256 is
  `53b9ac2906e7e961594e0c0d348840747c8b25faf5ada3e385bed7835c1468d9`.
  This prioritizes a bounded whole-payload AOT pass but remains discovery-only;
  it is not code/data truth or coverage evidence.
- The user's untouched multi-track Redump set has now been located and fully
  verified: Track 01 CRC32 `a0e9e5c2`, MD5
  `b9d48f139544051893f45eed62780e42`, and SHA-1
  `a7bb5cdafdf97ba1cd99cc832439f57ce0c39a8a`; all 13 audio tracks also match
  the manifest. Applying pinned `pal_track01.xdelta3` with hash-verified Xdelta
  3.2.0 produces the exact hueponik PAL target SHA-256
  `397b8a30f401bbd9d12904d6054946474ce4746bdf19565a8db534d65ba179e1`.
  Official hash-pinned `b3sum` independently verifies Redump Track 01 BLAKE3
  `8f7d08844df14b1afe8150fb0e434bf9c4f088834da65c8623025722d56586a5`,
  patch BLAKE3
  `c663c95edf41e844ef2c3e2d27c6409870aa7a2a245491b6549b325c744d7f62`,
  and patched Track 01 BLAKE3
  `f192d03f9fb94c91cf719ca796ffe1145471d9a8cec4c125c7f612ee0af306be`.
  The ignored prepared-disc receipt SHA-256 is
  `9e31857ad2d73f5740cacede8527373c57342f430be9f5ee75db981f5cb22200`.
  Independent source/target EXE-text comparison reports 84,728 changed bytes,
  21,912 changed aligned words, and 685 exact changed-word ranges. The largest
  contiguous range is `[0x8002271C, 0x80034848)` (18,507 words). This is byte
  provenance only; no code/data or semantic claim is inherited from the patch.
  The ignored delta receipt SHA-256 is
  `093649db4ba22cb93e2c5e5151b37c591d816d190cf43ea939c2a4be29b976c3`.
- The independently implemented memory/bus tranche passed a Sol Ultra audit and
  was reproduced byte-for-byte in the canonical tree. It now owns exact RAM
  mirrors, scratchpad, immutable caller BIOS, fail-closed MMIO routing, DMA
  origin rules, and catalog-bound code-page stamps that cannot be replayed
  across buses, resets, or A-to-B-to-A writes. Fresh canonical GCC Release and
  Clang ASan/UBSan builds both pass.
- The clean GTE oracle passed final Sol Ultra review at `67ffd6c` after fixes
  for signed MAC wrap visibility, RGB FIFO depth, command latch timing, and
  defensive register/timing boundaries. GCC/Clang Debug/Release, Clang
  ASan/UBSan, deterministic vectors, clang-tidy, and isolation all pass.
- The renderer-neutral GPU frontend passed Sol Ultra review at isolated commit
  `1cfabc4` and is reproduced byte-for-byte in the canonical tree. It now owns
  corrected retail texture-disable semantics, packet aliases, transfer packing,
  overlap ordering, status/read latches, immutable packet snapshots, and
  bounded constant-time queues. Canonical GCC Release and Clang ASan/UBSan pass.
- The bounded CD-ROM/CDDA controller slice passed Sol Ultra review at isolated
  commit `7062a5c` and is reproduced byte-for-byte in the canonical tree.
  Reason-coded IRQ packets, request-gated transfer FIFOs, sector sizing, track
  boundaries, CDDA mixing/autopause, and XA handoff are covered; BIN/CUE parsing,
  XA decode/resampling, subchannel detail, and physical drive timing remain
  explicit integration work. Canonical GCC Release and Clang ASan/UBSan pass.
- The MDEC tranche passed final Sol Ultra review at isolated commit
  `fa03a1453ba9876309fbe11ce69c2ef89e53e97f` and is reproduced
  byte-for-byte in the canonical tree. The review corrected opcode 4--7
  behavior, RLE termination and zig-zag semantics, IDCT indexing/rounding, DMA
  backpressure and parameter accounting, whole-word FIFO requests, cycle
  accounting, and UB-safe snapshots. Fresh canonical GCC Release and Clang
  ASan/UBSan tests pass. Remaining native-console precision and arbitration
  questions are explicitly documented in `clean-aot/mdec/README.md`.
- The software raster tranche passed final Sol Ultra review as a deterministic
  reference oracle. The review corrected integer-coordinate/top-left triangle
  coverage, 12-bit fractional color/UV interpolation, PS1 fixed-point line
  DDA and inclusive polyline joins, texture/STP/modulation/dither semantics,
  wrapped overlap transfers, clipping, malformed-state budgets and noexcept
  dispatch. It matches pinned hardware-capture digests for the complete
  triangle and UV/color scenes and all 147 overlap-copy interiors. This is not
  the production 100-Hz renderer: draw-to-display gating still needs the GP1
  display rectangle, and cache behavior, CRTC/interlace/24-bit scanout and the
  native backend remain explicit integration work.
- The pinned HD texture pack now has a strict metadata-only manifest: all 292
  present PNGs pass signature/chunk-CRC/IHDR validation and exact SHA-256
  inventory (2,890,854 bytes total). `Hashes.ini` declares 390 keys, so 98
  mappings are explicitly missing rather than silently treated as assets. The
  external two-hash filename semantics remain a behavior clue, not yet an
  accepted clean runtime lookup algorithm.
- The 100-Hz clock model has been re-audited against pinned hueponik,
  GooseStation, and DuckStation sources. Hueponik's PAL 100-FPS mode explicitly
  requires PAL x2 plus a heavy CPU overclock. GooseStation's PAL x2 doubles
  only the CRTC/video clock and states that at least 200% CPU is required;
  SPU and CD-ROM rates remain native. DuckStation implements the independent
  CPU multiplier by expanding system/CPU ticks while inversely converting
  device work, preserving 44.1-kHz SPU and media time. Therefore the generic
  machine needs an exact independent CPU-retirement multiplier. The previously
  asserted stock-only 338,688-cycle 100-Hz budget is retained only as the
  100% baseline, not as the product setting. The exact max-detail product
  multiplier remains a measured acceptance value; the tainted legacy lane's
  historical 900% setting is a hypothesis, not an inherited conclusion.
  Integration must also distinguish CPU-retirement time from native device
  time: feeding multiplied CPU totals into the CD-ROM slice's current fixed
  33.8688-MHz periods would incorrectly speed media up. Either scale every
  device deadline into the multiplied CPU timeline with exact carry, as the
  behavior reference does, or keep a separate native master-time coordinate
  and perform exact conversions at MMIO/event boundaries. Mixing the two is a
  release-blocking timing error.
- The generic machine clock has now passed Sol Ultra review after corrections
  for full-width rational arithmetic and frame-start retirement visibility.
  It accepts reduced positive multipliers through exact 10x, including
  fractional ratios, without overflowing an intermediate product. At exact
  100 Hz it emits 441 SPU samples per update and CDDA cadence `0,1,1,1` while
  CPU retirement advances independently. GCC and Clang Release plus
  non-recovering Clang ASan/UBSan pass; an independent arbitrary-precision
  oracle checked 189,989 boundary/random cases with zero mismatches.
- The clean SoC tranche is accepted under the parent Sol Ultra audit. The
  canonical code matches the reviewed implementation apart from safer CTest
  registration and passes fresh GCC `-O3 -Werror`, Clang ASan/UBSan, focused
  interrupt/DMA/root-counter tests, and its source-isolation scan. Its counter
  API explicitly advances in native device time, never multiplied CPU
  retirement time.
- A pinned GooseStation libretro behavior oracle now builds successfully under
  WSL and boots the exact prepared user disc through RetroArch 1.22.2. A
  10,000-frame PALx2 run at the historical 900% hypothesis reached a live demo
  race, reported 99.49 Hz and 44.1 kHz, and advanced exactly 100 seconds of core
  time. A recorded 4,000-frame race window contained 3,937 distinct rendered
  frame hashes and 55 consecutive duplicate images. Those image duplicates do
  not identify simulation duplicates, so 900% remains an oracle hypothesis,
  not an accepted product multiplier; a memory/state-level unique-update probe
  and deterministic controlled race are still required.
- The behavior oracle now has an independently derived seven-confirm replay
  from boot through FEISAR/Vector/Porto Kora and into a live controlled race.
  The 11,499-frame replay produces 11,500 lossless video packets; each sampled
  one-second live-race window at 93, 100, and 110 seconds contains 100/100
  distinct frame hashes. With a working dummy audio sink, the same run emits
  5,096,833 decoded stereo PCM frames at 44.1 kHz. That measured cadence is
  consistent with GooseStation's reported 99.49-Hz PALx2 behavior and is
  deliberately not substituted for the product's exact 100-Hz/441-sample
  contract. The metadata-only receipt is
  `docs/evidence/goosestation-palx2-route-v1.json` (SHA-256
  `d961b73926a26db24e870875931fa812625aea8fd272d7485bbb0084ec4d7a69`);
  the replay, capture, BIOS, and disc bytes remain ignored/untracked.

## Next concrete step

The live graphics boundary now loads the pinned Wip3out HD manifest, maps its
292 present entries into texture/font/banner title classes, verifies and
decodes PNGs, and explicitly retires stale Vulkan handles on source-generation
changes. Missing entries preserve the exact native scanout digest. Deterministic
3840x2160 4:3 and 5120x2160 21:9 receipts plus a real permitted-pack WIC/upload
probe are recorded in `clean-aot/app/evidence/hd-assets-visual-receipt.json`.
Permission metadata has a fail-visible `pending-details` receipt; creator,
date, scope, attribution, and evidence remain null until supplied rather than
being inferred. Next graphics work is the independently validated producer-side
VRAM texture/palette producer contract is now independently pinned to Beetle
PSX HW source and implemented over clean GPU/raster state: upload-rectangle and
CLUT CRCs identify manifest entries, resolved generation-safe handles feed
primitive sampling, and every unsupported or stale case remains native. The
derivation and deterministic live digest are recorded in
`clean-aot/app/evidence/beetle-upload-key-contract.json`. Next graphics work is
capturing broader clean game routes to measure manifest coverage and adding
hardware geometry sampling so the Vulkan path need not depend on CPU scanout.

Reconcile and commit the shared cleanroom integration as one reviewed product
state. First close or explicitly defer the known SIO, host, CD-ROM, SoC and
whole-machine acceptance findings; then rerun the aggregate GCC/Clang sanitizer
matrix and release-isolation/link-map gates. After that, complete the authorized
CFG/region manifest for the exact main executable plus the authenticated
`NFE.PB` entry-5 runtime variant, add the bounded raw-image compiler adapter,
and link one clean runtime/product executable with no legacy fallback. The
milestone ends when that executable advances its first authenticated guest
instruction and fails closed on every unknown PC or executable identity.

## Product decisions confirmed by the owner

- The deliverable is a normal modern-PC game: after initial lawful media setup,
  the player opens it directly and reaches the game without interacting with an
  emulator, compiler, BIOS menu, developer tool, or compatibility fallback.
- BIOS-to-game boot is part of the acceptance path. Directly jumping into a
  prepared executable is not an acceptable substitute for the finished product.
- The target experience is a 4K, exact-100-Hz, ultrawide-capable remaster of
  WipEout 3 Special Edition, with the full set of defensible GooseStation-class
  graphics improvements. A reduced first-class feature set or half-measure
  compatibility release is not an accepted endpoint.
- Hardware-precision gaps may be staged during development but may not be hidden
  behind permanent compatibility policy for release. Every known gap must be
  implemented, measured, or explicitly proven irrelevant to the complete game.
- The HD texture and font creator has granted permission. Record the creator,
  scope, date, source material, permitted uses, required attribution, and asset
  hashes in a durable provenance receipt before shipping or redistributing the
  assets; this handoff records the owner's confirmation but is not itself the
  permission artifact.

## Open blockers and unknowns

- No clean-AOT code has yet booted a BIOS or game instruction.
- ISO inventory found one PS-X EXE signature. The deterministic race now proves
  one finite high-RAM load from `NFE.PB` entry 5, one bounded low-RAM BIOS
  variant, and one exact main-EXE relocation into the BIOS patch window. These
  observations still require finite indirect targets, CFG closure, exact
  loader/patch reproduction, and replay across broader routes before release
  catalog acceptance.
- Hardware tranches now exist for GPU, SPU, CD-ROM, DMA/timers/interrupts, SIO,
  MDEC, memory, machine timing, media, raster and host presentation. Their
  focused tests are not whole-machine or hardware acceptance. Earlier audits
  rejected intermediate SIO, host, CD-ROM and SoC snapshots; later fixes and
  reviews must be mapped finding-by-finding before any release claim.
- No clean product executable links these tranches, generated regions and host
  presentation together. No clean-AOT code has booted the BIOS or game.
- The compiler has bounded region lowering and deterministic emission, but no
  complete reviewed game CFG, finite indirect-target closure, raw bounded-image
  adapter for `NFE.PB` entry 5, or 100% executable-epoch catalog.
- 100 Hz machine cadence exists and the GooseStation oracle supplies behavior
  evidence, but the product CPU multiplier, unique simulation-update proof,
  full-race timing, audio/media exactness and performance soak remain open.
- The live graphics drain now consumes generation-validated polygon provenance,
  applies explicit native/expanded projection and culling, perspective-correct
  UV/color interpolation, replacement sampling, and 4K/ultrawide scanout with
  byte-exact native fallback. The CPU/GTE/RAM producer still must publish those
  candidates for every shipping game polygon; incomplete provenance deliberately
  remains on the native raster path.

## Recovery verification (2026-08-21)

- Canonical product checkout `master` is preserved at `97bd3d7`, 95 commits
  behind `origin/master` (`768cdbf`), with locally advanced `psxrecomp` and
  `recomp-ui` submodules. It is the legacy/product lane, not the clean shipping
  implementation.
- `codex/remaster-cleanroom` is the clean integration branch. Nine isolated
  tranche branches remain divergent and should be treated as review receipts,
  not independently shippable products.
- Fresh aggregate GCC 16.1 Release configure/build succeeded from
  `clean-aot/CMakeLists.txt`; after correcting the aggregate to include media,
  runtime, and host, all 18 CTest targets passed.
- Fresh aggregate Clang 22.1.8 RelWithDebInfo with non-recovering ASan/UBSan
  built every component and passed the same 18 CTest targets with no sanitizer
  report.
- Fresh Python verification passed 118 tests and 584 subtests; one test was
  skipped by its declared environment gate.
- Those results establish buildability and regression-test health only. They do
  not supersede adversarial audit findings or establish hardware equivalence,
  static completeness, release isolation, game boot, race completion, 100-Hz
  correctness, or packaging readiness.
- The old project-recovery task failed before producing a report. This handoff
  is now the durable repository record of the reconstructed state.

## Ruled out

- Do not clean up or ship `d56`; its architecture and source history are tainted.
- Do not copy/cherry-pick Claude-era fixes, assets, tests, or conclusions.
- Do not count legacy interpretation, runtime compilation, menu runs, synthetic
  benchmarks, or duplicated presentation frames as static recompilation or
  100-FPS evidence.
- Do not conflate PAL x2 with whole-machine speedup or adaptive host pacing.
  PAL x2 is a 100-Hz CRTC cadence. An independent, fixed, exact CPU-retirement
  multiplier is required by the patch and must leave SPU at 441 samples per
  100-Hz VBlank and CDDA at three sectors per four frames. Do not accept the
  multiplier until a deterministic max-detail race proves 100 unique updates,
  1.00x wall/audio/media time, and no missed simulation deadlines.

## Authenticated bootstrap checkpoint (2026-08-21)

- The clean application now remeasures finalized setup, catalog, selection,
  BIOS, executable, CUE, and track inputs, mounts the measured disc, initializes
  the clean runtime at 100 Hz, loads the selected executable through its
  generated binding, and emits a canonical fail-closed bootstrap receipt.
- Synthetic end-to-end coverage reaches the first unknown generated PC and
  records zero semantic guest instructions; it intentionally does not convert
  control-flow dispatch into a guest-execution claim.
- The real executable authorization receipt is
  `clean-aot/catalog/evidence/executable-authorization.json` with status
  `partial-fail-closed`. It records 101,033 authorized main-EXE instructions,
  31,719 authorized NFE entry-5 instructions, and 3,013 verified BIOS
  direct-frontier instructions, but unresolved closure prevents a finalized
  selectable catalog and generated artifact.
- Consequently the deepest real product state remains authenticated evidence
  generation before runtime dispatch: BIOS instructions executed = 0 and game
  instructions executed = 0. Do not report BIOS or game boot until complete
  semantic generated regions and BIOS entry/reset-state binding exist.
- Fresh GCC and Clang ASan/UBSan application graphs each passed all 8 aggregate
  tests. Standalone host/render/GPU/raster passed all 4 tests; focused product
  and authorization Python tests passed 8 tests. Repository-wide pytest still
  collects five unrelated legacy `psxrecomp` failures (encoding/newline,
  obsolete assertions, and the live readram socket probe).

## Real BIOS reset execution checkpoint (2026-08-21)

- The runtime now has a distinct authenticated BIOS reset binding at
  `0xBFC00000`, immutable BIOS hash verification, generated CPU reset state,
  fail-closed generated dispatch, instruction accounting, and an explicit
  authenticated BIOS-to-game transition contract.
- `clean-aot/catalog/bios-reset-corridor.manifest` compiles only the 30
  cross-evidenced reset instructions. With the locally verified SCPH1001 BIOS,
  the generated region executed all 30 instructions, including two clean-bus
  MMIO stores, then stopped at the first excluded target `0xBFC00150`.
- The canonical real execution receipt is
  `clean-aot/catalog/evidence/bios-reset-execution.json`. This is the first real
  clean BIOS instruction execution in the repository: BIOS instructions = 30,
  game instructions = 0. No interpreter, JIT, legacy runtime, or fallback was
  linked or invoked.
- Next: cross-evidence and authorize the corridor beginning at `0xBFC00150`,
  extend generated load/store/COP0 semantics as encountered, then continue the
  same receipt chain until the authenticated game transition is reached.
- The next verified semantic frontier is 104 instructions in
  `clean-aot/catalog/evidence/bios-frontier-00000104.json`. Generated MTC0,
  MFC0, and RFE now operate on explicit COP0 status/cause/EPC/bad-address state.
  The real BIOS reaches `0x80000080` and fails closed because the RAM exception
  vector's copied-content epoch is not yet authorized.
- ADDI overflow semantics and successful same-PC generated dispatch advance the
  next deterministic frontier to 1,509 instructions across 67 dispatches in
  `clean-aot/catalog/evidence/bios-frontier-00001509.json`. The stop remains the
  exact fail-closed RAM exception vector `0x80000080`; no RAM content epoch or
  indirect target was inferred from the address alone.
- SLT/SLTU plus ANDI semantics advance authenticated execution to 17,353
  instructions and 3,232 dispatches. The new receipt
  `clean-aot/catalog/evidence/bios-frontier-00017353.json` stops without an
  exception at `0xBFC0703C`; this is a finite runtime return target inside the
  exact BIOS and can seed the next static direct-closure pass.
- Explicit COP0 breakpoint/debug-register state plus proved generated roots at
  `0xBFC03978` and `0xBFC00420` advance the deterministic real BIOS frontier to
  71,501 instructions across 12,282 dispatches. The canonical receipt is
  `clean-aot/catalog/evidence/bios-frontier-00071501.json`: 9,023 memory reads,
  13,452 memory writes, one device read, and 30 device writes. Its numeric stop
  PC is `0xA0000500` (an earlier handoff revision incorrectly rendered it as
  `0xA0000080`); the next tranche must authenticate the copied RAM execution
  epoch before binding that PC.
- The exact initial 64-KiB RAM epoch is SHA-256 `63dc69dd...a901c`; its bounded
  executable page `[0x500,0x1000)` is SHA-256 `99d536ee...e9570`. Distinct
  uncached and cached bindings preserve the BIOS transition
  `0xA0000500 -> 0x00000598` and invalidate on code-page mutation. After the RAM
  clear returns to BIOS and the proved `0xBFC042D0` root executes, the repeated
  frontier reaches 79,218 instructions and 13,830 dispatches, with 9,229 reads,
  14,991 writes, one device read, and 31 device writes. Receipt
  `clean-aot/catalog/evidence/bios-frontier-00079218.json` records the next RAM
  epoch SHA-256 `c1d13084...6b1744` and fail-closed PC `0x000000C0`, where the
  BIOS-installed low-RAM vector trampoline now begins.
- Exact-range epoch binding advances through the physical `0xC0` trampoline,
  dispatch table at `0x600`, selected handler at `0x540`, and exception-vector
  installer at `0xEB0`. Writes elsewhere on the same RAM page force a byte-hash
  recheck before refreshing the page stamp; any authenticated instruction-byte
  change still fails closed. Two repeated runs reach 79,382 instructions and
  13,864 dispatches with 9,253 reads, 15,013 writes, one device read, and 31
  device writes. Receipt `bios-frontier-00079382.json` stops cleanly at the next
  unbound physical RAM target `0x00006A70`; final 64-KiB RAM SHA-256 is
  `f1f4eeac...766c8`.
- The authenticated A0 call-44 stub at `0x00006A70` now transitions through
  the exact low-RAM A0 trampoline and service dispatcher to BIOS
  `0xBFC01920`. The compiler preserves `0xBFC0193C` both as the preceding
  jump's architectural delay slot and as an explicitly authenticated
  standalone entry; incidental edges into delay slots remain rejected. Two
  repeated probes first cross `0xBFC01940`, then reach 80,802 instructions and
  13,936 dispatches with 9,254 reads, 16,256 writes, one device read, and 34
  device writes. Receipt `bios-frontier-00080802.json` stops fail-closed at
  physical PC `0x000000B0`. The service-mutated 64-KiB RAM epoch is SHA-256
  `a8d34d5b...d4f4`; its zero-filled B0 target is deliberately not authorized
  as executable code.
- The zero-filled B0 diagnosis found a runtime hardware bug, not missing BIOS
  installation: A0/44 (`FlushCache`) sets COP0 Status IsC and issues 2,480
  cache-maintenance stores through cached KUSEG addresses. Those stores must
  target the isolated R3000A cache, but the runtime had forwarded them to RAM
  and erased the installed A0/B0/C0 vectors. Generated byte, halfword, and word
  stores now preserve RAM while IsC is effective; KSEG1 remains the uncached
  bus path. Adversarial coverage proves both behaviors.
- With the preserved exact epoch, B0 contains its installed trampoline to
  `0x000005E0`; the B0 dispatch table then selects the authenticated copied-RAM
  routine at `0x00000F2C`. Two repeated probes advance to 80,867 instructions
  and 13,947 dispatches, including 2,480 isolated-cache writes, and stop
  fail-closed at the next unbound copied-RAM PC `0x000027C0`. Receipt
  `bios-frontier-00080867.json` supersedes the hardware-invalid 80,802 receipt
  and records the new RAM epoch SHA-256 `c0020392...0cd3`.
- The `0x27C0` epoch executes 26 instructions before its own code writes change
  the whole-RAM identity to `39498839...036a5`; dispatch stops with
  `EpochMismatch` at `0x2150`, then resumes only after that new identity is
  compiled. Overlap reconciliation now rehashes exact prior ranges before
  disabling them, so unrelated same-page writes no longer discard the
  byte-identical A0/B0 trampoline; changed instruction bytes still invalidate.
- The refreshed epoch advances through copied-RAM initialization, immutable
  BIOS `0xBFC086B0`, B0 dispatch, and A0 service 0x2A. A subsequent RAM mutation
  stops before dispatch at `0x00003C90`. Two repeated runs reach 84,520
  instructions and 14,791 dispatches with 9,375 reads, 17,089 writes, 2,480
  isolated-cache writes, one device read, and 44 device writes. Receipt
  `bios-frontier-00084520.json` records fail-closed status `EpochMismatch` and
  next RAM epoch SHA-256 `6f30b27d...cc50b`.
- The `6f30b27d...cc50b` epoch is now authenticated only through exact function
  ranges `[0x3C90,0x3E78)`, `[0x28D0,0x2908)`, `[0x4360,0x4388)`,
  `[0x27A0,0x27BC)`, and `[0x2908,0x2958)`. The immutable BIOS return stub
  `[0xBFC06FDC,0xBFC06FE4)` is separately rooted; no containing RAM page was
  authorized. Two repeated probes reach 85,983 instructions and 14,874
  dispatches with 9,382 reads, 18,339 writes, 3,720 isolated-cache writes, one
  device read, and 48 device writes. Receipt
  `bios-frontier-00085983.json` stops fail-closed with `UnknownCode` at the next
  copied-RAM function `0x00002E00`; whole-RAM identity remains
  `6f30b27d...cc50b`.
- The exact copied-RAM function `[0x2E00,0x2E98)` reaches helper
  `[0x30C8,0x3108)`. Its first branch delay slot exposed missing static `SLTI`
  lowering; generated SLTI/SLTIU now use signed/unsigned comparisons against a
  sign-extended immediate instead of raising a false Reserved Instruction.
  Execution returns exactly to `[0x2958,0x2A64)` and stops at its direct helper
  `0x00003060`. The runtime's bounded copied-region table is 64 entries so this
  growing exact-range chain does not weaken into page authorization. Repeated
  probes reach 86,062 instructions and 14,893 dispatches with 9,388 reads,
  18,348 writes, 3,720 isolated-cache writes, one device read, and 48 device
  writes. Receipt `bios-frontier-00086062.json` records `UnknownCode` and the
  post-callback whole-RAM epoch `8af29f79...f1c9a`.
- Exact region `[0x3060,0x30C8)` under epoch `8af29f79...f1c9a` follows its
  observed loop and returns through the live `$ra` callback to `0x000031E8`;
  the dormant direct edge to `0x2FC8` is not speculatively dispatched. Two
  repeated probes reach 86,085 instructions and 14,899 dispatches with 9,391
  reads, 18,350 writes, 3,720 isolated-cache writes, one device read, and 48
  device writes. Receipt `bios-frontier-00086085.json` records `UnknownCode` at
  `0x000031E8`; RAM remains epoch `8af29f79...f1c9a` and COP0 reports no fault.
- The executed path through exact region `[0x31E8,0x33A4)` reaches only the
  exact A0/0x25 call stub `[0x6A90,0x6A9C)`. That stub traverses the existing
  authenticated A0 trampoline and BIOS service, then returns to the next
  unbound copied-RAM function at `0x00003108`; dormant edges remain rejected.
  Two repeated probes reach 86,291 instructions and 14,939 dispatches with
  9,422 reads, 18,369 writes, 3,720 isolated-cache writes, one device read, and
  48 device writes. Receipt `bios-frontier-00086291.json` records `UnknownCode`;
  RAM remains epoch `8af29f79...f1c9a` and COP0 remains fault-free.
- Exact region `[0x3108,0x31E8)` explicitly roots `0x31A0`, which is both a
  live branch target and another branch's architectural delay slot; no general
  delay-slot relaxation was added. Its live path reaches only A0/0x17 stub
  `[0x6B10,0x6B1C)`, then immutable BIOS comparison routine
  `[0xBFC03288,0xBFC03310)`. The service returns to copied RAM `0x0000283C`
  after mutating the whole-RAM epoch to `9548d3d4...4a35d`; stale authorization
  is rejected there. Two repeated probes reach 86,834 instructions and 15,057
  dispatches with 9,521 reads, 18,423 writes, 3,720 isolated-cache writes, one
  device read, and 49 device writes. Receipt `bios-frontier-00086834.json`
  records the new `UnknownCode` frontier with fault-free COP0 state.
- Under epoch `9548d3d4...4a35d`, only continuation `[0x283C,0x2870)` is
  authenticated. It calls the already-exact `0x4360` helper, then reaches A0
  service 0x96 through exact stub `[0x6AA0,0x6AAC)`; the subsequent A0/0x97
  call remains dormant and unbound. The live service enters only immutable BIOS
  wrapper `[0xBFC085B0,0xBFC085D4)` and returns to unbound copied RAM
  `0x00003C2C`. Two repeated probes reach 86,892 instructions and 15,069
  dispatches with 9,524 reads, 18,432 writes, 3,720 isolated-cache writes, one
  device read, and 50 device writes. Receipt `bios-frontier-00086892.json`
  records `UnknownCode`; RAM remains `9548d3d4...4a35d` and COP0 is fault-free.
- Fresh epoch-`9548d3d4...4a35d` bytes authenticate only `[0x3C2C,0x3CE0)`
  (SHA-256 `814b8e26...6a705`) and its live A0/0x2A stub
  `[0x6B20,0x6B2C)` (SHA-256 `ad7c5bc5...2876d`). A0/0x2A traverses the
  existing exact BIOS path, performs another 1,240 isolated-cache writes, and
  mutates RAM to epoch `9898a802...6e6d`. Execution then reaches formerly
  dormant A0/0x97 stub `0x00006AB0` and rejects it because it has not been
  authenticated in the new epoch. Two repeated probes reach 88,864
  instructions and 15,237 dispatches with 9,614 reads, 19,757 writes, 4,960
  isolated-cache writes, one device read, and 53 device writes. Receipt
  `bios-frontier-00088864.json` records fault-free `UnknownCode` at `0x6AB0`.
- Epoch-`9898a802...6e6d` A0/0x97 stub `[0x6AB0,0x6ABC)` is authenticated
  by exact SHA-256 `dd2df7ea...47d07`. Its sole live immutable wrapper is
  `[0xBFC0C1FC,0xBFC0C220)`, which enters the already-authenticated service path;
  adjacent A0 and BIOS wrappers remain rejected. The service performs another
  1,240 isolated-cache writes, mutates RAM to `315de527...d56fa`, and stops at
  the next unbound immutable BIOS target `0xBFC00DA0`. Two repeated probes reach
  90,914 instructions and 15,420 dispatches with 9,710 reads, 21,094 writes,
  6,200 isolated-cache writes, one device read, and 57 device writes. Receipt
  `bios-frontier-00090914.json` records fault-free `UnknownCode` at that target.

- Frontier 89: the authenticated second corridor `[0xBFC00150,0xBFC0023C)`
  executes another 59 instructions, including memory-control and cache-control
  initialization, then rejects the unbound `MTC0` PC at `0xBFC0023C`. The
  repeated deterministic receipt is
  `clean-aot/catalog/evidence/bios-frontier-00000089.json`.
- Do not commit disc images, retail BIOS data, patched executables, captures,
  generated C, screenshots, saves, or packaged binaries.

- Registry capacity is no longer authorization: runtime construction now owns
  a fixed manifest-sized registry (default 512, strict upper bound 4096),
  allocation/configuration fails closed, invalidated slots are reused, and all
  binding range/hash/stamp checks remain exact. More than 256 exact bindings
  are covered by the runtime adversarial test.
- Immutable BIOS shard `[0xBFC03FAC,0xBFC043AC)` (SHA-256 `a9a3f29f...50027`)
  now has an exact ROM contract and advances through `0x18BC` and `0x19C8` to
  copied-RAM `0x1858`. Receipt
  `bios-shell-registry-frontier-00001858.json` records 19,270,447 instructions,
  1,787,127 dispatches, zero CD MMIO, and fail-closed compiler rejection at
  `0x1858`: `control transfer has no bounded delay-slot instruction`.
- Frontier `0x1858` was an exact truncation: `JR $ra` at `0x1C54` owns
  `ADDIU $sp,$sp,64` at `0x1C58`. Manifest end moved by exactly one word to
  `0x1C5C` (SHA-256 `5d370976...1e0bd`) with compiler atomicity unchanged.
  A broad overlapping `0x17F4` recompilation was rejected; the accepted live
  function is exactly `[0x17F4,0x1858)`. Fifty exact frontiers now reach
  `0x2588`: 19,270,583 instructions, 1,787,197 dispatches, zero CD MMIO and
  zero sectors. Receipt: `bios-shell-exact-frontiers-00000139.json`.
- Exact frontiers 140 through 239 advance from `0x00002588` to fail-closed
  `UnknownCode` at `0x80050F8C`. The repeated receipt is 19,271,314
  instructions, 1,787,439 dispatches, 5,008 device reads, 56,404 device
  writes, 54,328 SPU writes, and zero CD MMIO or raw sectors. RAM changes at
  frontier 146 from epoch `3515fab5...056` to `4d473637...7a`; every later
  copied-RAM binding uses the latter exact stamp. Frontier 209 reuses resident
  shell shard AUTO82 only after its complete range/hash matches. Frontier 210
  has different authenticated bytes at an already-used guest PC, so generated
  native symbols now optionally use the full variant identity; unknown symbol
  namespace modes fail closed. Continue by authenticating only the live path
  at `0x80050F8C`. Receipt:
  `clean-aot/catalog/evidence/bios-shell-exact-frontiers-00000239.json`.
- Frontiers 240 through 267 advance from `0x80050F8C` through copied RAM and
  the exact immutable BIOS return at `[0xBFC0D8E0,0xBFC0D8EC)`. Context-aware
  generation now sources BFC addresses from the verified BIOS identity rather
  than the RAM aperture. The BIOS return reaches the already-authenticated
  low-RAM vector-handler entry `0x000005C4` and remains continuously
  executable until the unchanged three-million-dispatch diagnostic bound.
  Two probes match at 23,515,329 instructions, 3,000,000 dispatches, RAM epoch
  `46ae2725...d52f`, and zero CD MMIO/raw sectors. This materially different
  live-handler state is not `UnknownCode`; diagnose its indirect vector target
  and interrupt/event source before extending frontiers or bounds. Receipt:
  `clean-aot/catalog/evidence/bios-shell-exact-frontiers-00000267.json`.
- Live-shell frontier advancement has an opt-in authenticated straight-line
  batch mode: `advance_live_shell_frontiers.py --max-batch-instructions N`.
  It keeps the default at one, never crosses a later control transfer, keeps a
  frontier control transfer and delay slot atomic, and derives the variant
  identity from the same exact source-epoch hash and enlarged range. This
  reduces aggregate CMake relinks on linear corridors while leaving compiler
  CFI rejection authoritative. The committed shard-371 receipt audits as 103
  unique exact bindings covering 116 instructions.
- A second opt-in mode, `--max-direct-closure-instructions N`, emits one
  authenticated multi-range binding across mandatory plain-`J` corridors.
  It never follows conditional branches, JAL/JALR, JR, exceptions, or an
  out-of-image target, and always retains the control-transfer delay slot.
  Calls/returns therefore remain live-evidence and live-RA qualified instead
  of gaining dormant authorization. Historical exact-contiguous projection on
  the shard-371 receipt reduces 103 relink points to 29 (3.5517x frequency).
# 2026-08-22 accelerated shell frontier checkpoint

- Adopted the compiler acceleration from `02b712c` with
  `advance_live_shell_frontiers.py --max-batch-instructions 16`.
- Reconciled durable binding 410 (`0x800476CC-0x800476D0`) directly into
  binding 411 (`0x800476D0-0x800476D8`): no gap and no duplicate identity.
- Exact bindings 411-422 advanced 340 authenticated instructions before the
  materially different terminal condition. The repeated terminal is the known
  `BFC0D8E4 -> A0 -> 0x5C4 -> BFC0D8E0` service SCC at the unchanged
  3,000,000-dispatch bound, not UnknownCode. CD reads/writes/raw sectors remain
  zero. Canonical evidence is
  `clean-aot/catalog/evidence/bios-shell-live-frontiers-800476d0-bfc0d8e4.json`.
- Next bounded action: diagnose why live service index `a0=0xA0` repeatedly
  returns through this SCC with `Cause=0x1C`, `EPC=0x8005027C`, and
  `$ra=0x00001B10`; do not raise the dispatch bound.
- Windows Clang ASan aggregate tests require a 16 MiB PE stack reservation for
  the CD-ROM and SIO test harnesses. The reservation is independent of local
  sanitizer toggles because aggregate flags are injected by the parent build.

## d066748 product regression rebinding (2026-08-22)

- Rebuilt `Wipeout3SE.exe` from an isolated detached worktree at exact canonical
  commit `d066748`, so concurrent uncommitted runtime/frontier work was not
  consumed. The executable SHA-256 is
  `e7fa1a7046028dca43e4ab664a792f3ee78bc0e232993505dff379240600d8ab` and
  the package manifest SHA-256 is
  `56905a3e329b137c0f9039f1a478eedef06d9da13c40155aa254918702f1bb9e`.
- Rebound setup, settings, dual-card persistence, Windows installed launch,
  graphics matrix, and all four deterministic 4K/5120x2160 soak receipts. The
  two-stage installer contains exactly 135,829 authenticated game instructions
  in 290 regions with zero exclusions and zero unresolved CFI sites. Its
  runtime payload independently passes compiler/patch/oracle/legacy leakage and
  SDL3 DLL-closure audits after a Unicode-path atomic relocation.
- All 32 product tests pass. Release readiness remains honestly fail-closed at
  12/15: direct BIOS-to-game execution, all 64 behavioral routes, and
  authenticated media execution are the only failures. Physical present timing
  remains `unavailable`; no readiness claim or timing waiver was introduced.

## Portable single-entrypoint delivery checkpoint (2026-08-22)

- Installer/Setup delivery is retired. The authoritative package is a portable
  folder with exactly four manifested payloads: root `Wipeout3SE.exe`, root
  `SDL3.dll`, `policy/default-config.json`, and the clean generated frontier
  artifact. No batch launcher, Setup executable, compiler, patcher, BIOS, CUE,
  BIN, registry installation, or wizard ships.
- Root `Wipeout3SE.exe` now owns native first-run BIOS/CUE selection. It verifies
  the exact BIOS identity and all 14 externally referenced track sizes, writes
  only external path references plus hash-only evidence using atomic Unicode-safe
  replacement, and leaves a read-only relocated source folder unchanged.
  Cancellation creates no media state. Portable acceptance used lawful external
  inputs and is bound to executable SHA-256
  `ceb9cbf7607d9586344adf54df23ec4991ae42762792af06bf738aa85c820e44`
  and package manifest SHA-256
  `1cd3b6a3479b546d3bc63dfe543d9bca48300065cdacbdd96e1512e551f3499f`.
- All 32 product tests, DLL/source/link isolation, settings, persistence, four
  4K/ultrawide 100-Hz soaks, and graphics matrix pass. Readiness remains 12/15;
  only direct game execution, 64 behavioral routes, and authenticated media
  execution remain failed.

## Standard PSXRecomp product-path pivot (2026-08-22)

### Independent clock-domain candidate (2026-08-23)

- The protected, user-verified stock executable remains
  `build-psx-standard/Wipeout_3_Special_Edition__Recompiled.exe`; it has not
  been overwritten. The user verified its boot, FMV, memory-card, countdown,
  geometry, and stock race behavior as flawless.
- The current experimental implementation separates raw CPU retirement from
  native device time. At the requested 900%, live release telemetry measures
  `cpu_effective=9.001x`; the device clock remains 33.8688 MHz and SPU output
  measures 44,100.0 frames/s relative to that native clock. Device-service
  charges and proven idle skips do not receive CPU-overclock scaling.
- Gameplay acceleration switches the CRTC/VBlank target to 100 Hz while the
  sustained 3D gate is active. A debug-tools build was throughput-bound at
  roughly 62-75 VBlanks/s. The Release configuration with debug tools and
  rewind disabled sustains approximately 99.5-100.2 VBlanks/s, 9.001x CPU
  retirement, a 33,868,800-cycle native device second, and 44,100.0 SPU
  frames per native second. This proves clock delivery, not yet race-simulation
  fidelity.
- The original overlay divided delivered VBlanks by 59.94 even when the active
  target was 100 Hz, falsely displaying about `1.67x`. The corrected candidate
  divides by `interrupts_get_vblank_rate()`, so 100 delivered at a 100-Hz
  target displays `1.00x`. It builds separately at
  `build-psx-clock-rc/Wipeout_3_Special_Edition__Recompiled.exe`; the user's
  live test process was not disturbed.
- Focused existing machine, geometry, SPU, CD-audio, game-transition, and
  memory-card tests all pass (6/6) after the clock-domain changes. These tests
  are regression evidence only; they do not replace a controlled live race.
- The user reports FMV and CD audio now play at correct speed and that gameplay
  is high-framerate but feels too fast. Do not tune away the explicit 9x CPU
  requirement based on the obsolete counter. Next obtain direct evidence from
  a controlled 100-Hz race: compare the HUD race timer to wall time while the
  same interval's telemetry proves 100-Hz VBlank, 9x retirement, native device
  time, and 44.1-kHz SPU. Also verify that the current sustained-world-3D gate
  does not accelerate non-race menus in a way that violates stock behavior.

### Validation launch invariant (2026-08-23)

- Never open the launcher during iterative runtime validation. Launch the
  already-configured executable with `--no-launcher`; the retail BIOS, disc,
  card, controller, and video selections are already persisted. Re-learning
  this or attempting UI automation wastes the user's time and produces no
  gameplay telemetry.
- Protected stock executable remains
  `build-psx-standard/Wipeout_3_Special_Edition__Recompiled.exe`. Timing work
  builds and runs only from `build-psx-clock-exp` until fully verified.

- The user explicitly retired the clean-AOT-only, genuine BIOS-shell boot, and
  no-interpreter/runtime-compiler boundaries. They were not product
  requirements. The shipping architecture is the established PSXRecomp model:
  HLE boot skip, statically generated base-game code, and the framework's
  capture/compile/cache safety path for overlays and unknown code.
- Preserve `clean-aot/` only as historical research. It is not a source for
  shipping code, feature design, or acceptance evidence. Do not spend further
  shipping work advancing or porting its BIOS, hardware, rendering, timing,
  input, audio, packaging, or verification implementations.
- A fresh `build-psx-standard` Release build from the root generated project
  completed successfully. A bounded headless run with the prepared 14-track
  local test disc reached frame 3,486 and visually rendered the city intro;
  `baseline-frame.png` shows the Psygnosis logo and
  `baseline-frame-20s.png` shows the intro. These build-local screenshots are
  evidence only and must not be committed.
- Root CMake now disables `PSX_SETUP_WIZARD` and no longer passes
  `ENABLE_SETUP_WIZARD`. The normal single-executable recomp-ui launcher stays
  enabled for settings and external BIOS/disc selection; the separate
  Generate & Rebuild setup-host surface is not part of the player product.
- Next: verify the no-wizard launcher from a clean portable directory with no
  cached paths, require/select external retail BIOS and disc without copying
  either, then exercise menus and race routes through the debug protocol.

## Inherited-runtime distrust and upstream reset (2026-08-22)

- The user explicitly rejected inherited repository implementations and old
  receipts as trustworthy evidence; the failed historical project is why this
  refactor exists. A boot screenshot is only a smoke result, never proof of
  correctness, completeness, remaster quality, or performance.
- The pinned framework fork was 21 commits behind `mstan/psxrecomp` master and
  carried one 954-line local patch (`afe4c690`) mixing compiler behavior with
  bespoke route/freeze tooling. Current official upstream independently
  contains the important PGXP newline, load-delay scope, CPS fallthrough,
  dirty-text continuation, overlay-capture, and SPU corrections as smaller
  commits with focused tests.
- The submodule is reset to current official upstream
  `cf918c7be5e0926ec47c0e709e7070b6ff535a37`, plus the one-line test-harness
  correction described below (`d65e88f3`). A fresh isolated Release build
  (`build-psx-upstream-audit`, debug tools enabled) completed with no local
  production-runtime patch.
- Fresh external retail BIOS plus full 14-track disc execution reached frame
  3,483 and rendered the city intro. This proves only upstream baseline
  viability. All menus, races, overlays, graphics features, timing, audio,
  input, saves, and packaging still require new evidence.
- `game.toml` now sets `openbios = false`, matching the product decision that
  players provide the exact retail BIOS. A clean-directory probe with no BIOS
  failed closed at “Step 1 of 2”; supplying the BIOS but no disc failed closed
  at “Step 2 of 2”; supplying both reached the intro without copying either
  input into the application directory.
- The user subsequently made the reset absolute: old project work is treated
  as failed and useless for this refactor. Implement the remaster independently
  on the audited upstream base; do not inventory old fixes for selective reuse.
- Upstream's full runtime test build initially exposed an unlinked
  `gpu_ws_precise_nclip_enabled` dependency in `gte_register_access_test`.
  Branch `codex/wipeout3-upstream-audit` commit `d65e88f3` supplies the missing
  test stub. After that one-line harness fix, all 38 enabled upstream tests
  passed; three explicitly disabled upstream tests remain unproved and must not
  be represented as coverage.
- New acceptance requirements for startup intro skipping, memory-card startup
  acceleration, and race-load acceleration are in
  `docs/REFACTOR_ACCEPTANCE.md`. Each requires normal-vs-accelerated
  byte/order/state equivalence and fail-closed fallback; generic speed claims
  or screenshots are insufficient.

## Verified 100 Hz race and post-race replay (2026-08-23)

- The user completed a full race with the current clean-room timing candidate
  and reported no visual, timing, audio, geometry, countdown, or gameplay
  issue: “seems flawless afaik.” The game remains running as PID 34412 at the
  post-race replay; do not stop that exact process until the user finishes.
- Live telemetry across the completed race/replay held approximately 100 Hz
  guest VBlank and 44.1 kHz SPU output with no sustained audio underruns or
  overflows. Exact clock receipts continue to report 9.001x CPU retirement
  while device time remains native. One-second 93/107 Hz wall samples were
  reciprocal scheduler jitter and returned to 100 Hz without accumulated
  simulation drift.
- The user wrote a new post-race replay state at
  `saves/scph1001/state_80162474_slot02.pst` (2026-08-23 03:37:48). This is now
  the authoritative replay acceptance state; slots 0 and 1 are older.
- Runtime startup now prints a compact overlay cache audit. The coherent cache
  tag is `cg10_f4a6ebf3_gc7399b41b_f0`; six DLLs are indexed with status `ok`.
- Offline extraction of slot 2's zlib-compressed 2 MiB RAM proved that captures
  at 0x80004000 and 0x8000D000 match byte-for-byte and are admitted natively.
  The four large regions at 0x800C0000, 0x800CE000, 0x800D1000, and 0x800D4000
  are distinct race/replay content variants (2.9-12.9% byte matches), so their
  old DLLs are correctly rejected and those regions remain interpreted.
- `build-psx-clock-perf/overlay_captures.slot2.json` is a derived 10-record
  additive manifest: the original six records plus four exact slot-2 variants.
  It was generated without touching the live process. Once the game is closed,
  compile this manifest against the same runtime/recompiler identity, relaunch
  slot 2 with `--no-launcher`, and require native non-BIOS PCs plus unchanged
  100 Hz/9.001x/44.1 kHz telemetry and the same flawless replay behavior.
- Regenerating static game C removes the title-specific call to
  `psx_idle_batch_countdown` at 0x8015FAA4. It was restored locally in
  `generated/SCES_028.45_full_139.c`; telemetry again reports idle skips.
  Before final delivery, make this emission durable in the generator/config so
  a clean regeneration cannot silently lose the compactor.

## Deep review corrections -- authoritative current state (2026-08-23)

This section supersedes the two earlier 2026-08-23 timing sections wherever
they disagree. Those sections mixed valid user observations with claims that a
fresh source/build/launch audit could not support.

### Current verdict and protected baseline

- The complete remaster goal is **NO-GO**. The timing foundation is materially
  better and has a fresh bounded boot proof, but it has not yet passed a new
  interactive FMV/menu/card/race/post-race/CDDA run. Actual 8 MiB RAM, full
  GooseStation-equivalent PGXP, the complete aspect/resolution matrix, and the
  licensed bezel/font/text payload are also not implemented.
- The protected user-verified stock executable remains
  `build-psx-standard/Wipeout_3_Special_Edition__Recompiled.exe`, SHA-256
  `00f55955667df98653e2aa05903a8601a811bafaefa7d27d64c437ecd32b220d`.
  It was not overwritten.
- No old gameplay process remains active. PID 34412 and its instruction not to
  stop it are historical. The latest post-race replay state remains useful test
  data, not a condition that may halt engineering progress.
- The current experimental executable is
  `build-psx-clock-v2/Wipeout_3_Special_Edition__Recompiled.exe`, SHA-256
  `d671b36c0c76492298b6c2f8dd6b48a3d91b16232c97577d3c49d168d3fd9807`,
  size 43,961,150 bytes. It is a separate Release build with PGXP disabled.

### Timing architecture that survived review

- Raw CPU retirement and native device time are distinct domains. At 900%, an
  exact reduced-rational converter advances one native tick for every nine raw
  retired CPU cycles and carries the remainder. GTE and multiply/divide
  completion remain on raw CPU time; CD-ROM, SPU, SIO, timers, DMA, MDEC and
  VBlank remain on native 33.8688-MHz device time.
- PALx2 uses the title CRTC ratio directly rather than a synthetic 100.000-Hz
  integer clock: `53,203,425*2 / (3406*314) = 99.493634313370 Hz`. Fractional
  phase is carried, producing alternating 340,411/340,412-native-cycle frames.
  CD audio and SPU are not scaled with the VBlank multiplier or CPU overclock.
- Driver VSync is accepted as the pacer only when the host display is within
  0.2% of the requested rate; otherwise wall-clock pacing uses the exact CRTC
  ratio. Headless mode intentionally runs unpaced, so its wall-throughput
  `speed` field is not interactive pacing evidence.
- Telemetry now reports requested CPU overclock, exact effective raw/native CPU
  ratio, native device time, SPU rate, and independent CDDA sector rate. The
  prior overlay divided VBlanks by a fixed 59.94-Hz constant and therefore
  could not be trusted at PALx2.

### Defects found and repaired by the review

- `dirty_ram_interp.c` and memory-load cycle charges bypassed the CPU
  raw-to-native converter by incrementing native `psx_cycle_count` directly.
  Both production paths now use `psx_advance_cycles`; this was a real reason
  earlier 900% claims were not reliable. All remaining direct native-clock
  increments were audited and are intentional device/replay/helper paths.
- The title region helper returned `"(Europe)"` for SCES, while the new clock
  gate compared only against `"PAL"`. The launch path could therefore reject
  PALx2 despite a successful compile. PAL/Europe/Australia labels and SCES/SLES
  serials now select the PAL CRTC.
- The generator source contained the WipEout countdown compactor but the
  generator executable and generated C were stale. The generator was rebuilt,
  its patch tests passed, and clean regeneration now emits the exact guarded
  site at `0x8015FAA4`; no manual generated-C repair remains part of the plan.
- The countdown compactor computed its wake distance from a device clock that
  may lag CPU time on the deadline fast path. It could compact across an event
  and delay the guest CPU's observation. It now uses the authoritative absolute
  next-service deadline minus current CPU-native time. A regression proves a
  CD event at native cycle 5 leaves the final countdown iteration unbatched.
- Persisted overlay/data-shard durations and load-acceleration horizons are
  native elapsed intervals and no longer pass through CPU-overclock scaling.
  PDS1 data shards are conservatively disabled away from stock CPU ratio
  because that format does not preserve raw execution cost.
- Save creation now flushes pending batched CPU work before serializing CPU,
  RAM, devices, and clocks, preventing a pre-device/post-clock mixed snapshot.
  Save format v6 carries CPU/native clocks, rational remainder, CRTC phase, and
  configuration identity; older v5 slot files are deliberately rejected.

### Fresh verification receipts

- The overlay compiler rebuilt all six exact shards: 6 built, 0 skipped, 0
  failed. The current coherent cache tag is
  `cg10_504c3ad7_gce5e5eff6_f0`. Startup's `indexed=6 candidates=0 status=ok`
  means six DLL artifacts are indexed and zero function candidates are loaded
  *yet*; candidates load lazily and this is not an empty-cache result.
- All 39 enabled runtime tests pass. Three registered tests remain explicitly
  disabled and are not evidence: `sio_dualshock_rumble`, `spu_fidelity`, and
  `sio_card_protocol`. Focused Clang UBSan/Werror timing tests pass 3/3. A full
  Clang runtime build remains blocked in third-party libchdr by a duplicate
  `Byte` typedef under Werror, so there is no full-sanitizer claim.
- A 15-second isolated headless boot of the exact current executable used the
  external BIOS/disc, `--no-launcher`, software rendering, and a fresh temporary
  card directory. It reached 1,274 VBlanks without a crash and reported:
  `title_crtc=PALx2`, `effective=99.493634Hz`, `cpu=900%`, every interval at
  exactly `cpu_effective=9.000x`, and SPU intervals at approximately 44,100 Hz.
  No CDDA was active during this smoke, and headless mode was unpaced; therefore
  this is a boot/config/accounting proof only, not a gameplay or CDDA proof.

### Remaining correctness and product blockers

- Runtime RAM is still hard-coded as 2 MiB in memory, boot-state, shard,
  interpreter, cosim, and PGXP address paths. Any earlier implication that the
  product already had actual 8 MiB RAM was false. Implement and adversarially
  verify one coherent address/mirroring/save-state contract before claiming it.
- Save loading is not transactional. Sections mutate the live machine as they
  are read, so a late malformed section, clock-policy mismatch, or resume-PC
  rejection can leave partially restored state. Reject duplicate sections,
  require the full v6 section set including I-cache, validate the complete
  timing policy before mutation, and load into staged state or provide exact
  rollback before treating save-state failure as safe.
- Full/max PGXP is not implemented and remains disabled in this candidate.
  Missing pieces include unified capability gating, generic precise culling,
  complete texture/color/depth correction, correct vertex-cache semantics,
  provenance invalidation across AOT/overlay/interpreter/load-delay paths,
  projection preservation, tolerance/2D/transparent-depth/threshold controls,
  and PGXP save/runahead state. Compare behavior against pinned GooseStation
  builder v0.6.9.11609 (`a233ec1f`) rather than marketing labels.
- 4:3 is the current product aspect. Launcher support stops at 4:3/16:9/21:9
  and scale 1-4; 32:9 and verified resolutions through 8K are unfinished.
  The renderer also fuses display and projection decisions in places that must
  be separated before claiming arbitrary aspect correctness.
- `assets/provenance/hd-asset-permission.json` remains
  `pending-details`; permission fields are null and no licensed payload is
  present. Bezel, HD font, and HD text work cannot be claimed or shipped.
- Next acceptance action is a fresh interactive `--no-launcher` run through
  FMV, menu/card check, countdown, a complete PALx2 race, CDDA, and post-race
  replay with telemetry captured. The replay state is a start/stop data source,
  never a reason to pause the goal. After that, make save loading transactional
  before beginning the separate 8-MiB and PGXP workstreams.

### Ruled-out approaches

- Do not scale the whole machine or multiply native device time to simulate CPU
  overclock; that changes CD/SPU/SIO/timer behavior.
- Do not use Q16 approximations where the hardware ratios are rational and can
  carry an exact remainder.
- Do not gate PALx2 from renderer heuristics or a sustained-3D detector; the
  configured title CRTC owns the clock continuously.
- Do not hand-edit ignored generated C. Rebuild the generator, test it, and
  regenerate from `game.toml`.
- Do not treat a v5 save as authoritative for a v6 runtime, or a headless
  throughput counter as proof of interactive pacing.

## Post-review implementation checkpoint (2026-08-23 06:00 PDT)

This section supersedes the prior "next acceptance action" ordering. The
post-race replay is a reusable start/stop workload only; it must never pause the
goal. Fresh boot reproduces the FMV bottleneck without loading the old state.

### Preserved live and binary state

- The protected stock binary is unchanged at
  `build-psx-standard/Wipeout_3_Special_Edition__Recompiled.exe`, SHA-256
  `00F55955667DF98653E2AA05903A8601A811BAFAEFA7D27D64C437ECD32B220D`.
- The user-tested v2 binary remains
  `build-psx-clock-v2/Wipeout_3_Special_Edition__Recompiled.exe`, SHA-256
  `D671B36C0C76492298B6C2F8DD6B48A3D91B16232C97577D3C49D168D3FD9807`.
  Its current user process is PID 4316 and must not be stopped by diagnostics.
- The unpromoted v3 binary is SHA-256
  `5AF234C839629FC9D67763BFF79FAE957421D8A4BAC25E54F7156194EAB873C9`,
  cache identity `cg11_6ce69608_gce5e5eff6_f0`. It is a failed performance
  candidate, not a release or interactive-test candidate.

### v3 FMV evidence and architectural diagnosis

- The raw/native rational clock is exact at `cpu_effective=9.000x`, but steady
  FMV headless throughput is only about 48-58 VBlanks/s (`0.49-0.59x`) against
  the PALx2 target. SPU remains near 44.1 kHz; MDEC completes about 12-15
  decodes/s. The host retires roughly 150-177 million raw guest cycles/s, only
  16.7-19.7 million native-equivalent cycles/s versus the required 33.8688
  million. This is host execution throughput, not a CD/SPU clock error.
- Allowing the exact guarded title countdown compactor during MDEC is correct
  and event-bounded, but saves only about 0.85-3.95 million native cycles/s in
  the steady movie. It is a partial optimization and does not explain the
  remaining factor-of-two deficit.
- Twenty all-thread GDB samples put the main thread in `psx_dispatch_impl`
  20/20, scheduler 19/20, dirty/static dispatch 18/20. Guest functions
  `801182F4` (8 samples) and `801647D8` (4 samples) repeatedly cross tiny AOT
  blocks, calls, memory helpers, I-cache checks, and full interrupt checks.
  The next performance change must reduce this structural overhead while
  preserving instruction-boundary IRQ delivery; do not add another timing
  shortcut or scale native devices.

### Transactional v6 state loading

- The entire section stream is decoded and inflated before apply. Unknown,
  duplicate, trailing, missing, wrong-sized, configuration-mismatched, and
  unsafe-resume states reject before mutation. ICACHE and exact dirty-bitmap
  geometry are mandatory.
- A full rollback image covers CPU, RAM/scratch, IRQ/CRTC phase, timers, clock
  carry, GPU/VRAM and its host mirror/dirty metadata, SPU/RAM, CD, DMA, SIO,
  MDEC, dirty-code bitmap, and I-cache. Late mutating subsystem failure and a
  commit-time resume-PC rejection restore the pre-load machine.
- GTE/PGXP canonicalization, RAM blessing, and overlay invalidation happen only
  after every rejection gate. Tests require zero precision invalidations on a
  rejected load and exactly one on success.
- The review found and fixed a save-side ordering defect: deferred device work
  was serviced only when the later clock section was written, after RAM/IRQ had
  already been serialized. Saves now publish CPU work and service due devices
  before the first section, so a due DMA is included in RAM. A regression makes
  that ordering observable.
- The latest slot-2 replay is v5 (8-byte clock, 12-byte IRQ) and intentionally
  cannot migrate to the v6 timing contract. Keep using v2 to inspect it; new
  candidates boot fresh and create v6 states.

### PGXP correctness tranche and remaining boundary

- Codegen identity is now 11. A codegen-hash guarded `gpr_written` callback
  invalidates provenance for architectural/synthetic writes even when the
  integer value is unchanged. AOT, full-function BIOS, strict/overlay emitters,
  dirty-RAM and standalone interpreters cover link writes, COP0/CFC2 writes,
  synthetic widescreen writes, and the BIOS jump stub.
- Destructive AND/XOR/NOR/SLT(U) and immediate forms now replace stale
  destination provenance instead of relying on value validation. Focused PGXP,
  emitter, structural, and patch tests pass.
- This is not full PGXP. Bitwise/comparison provenance is still conservative
  invalidation rather than GooseStation's richer half-wise propagation;
  same-value DMA/host memcpy writers can retain stale memory provenance; full
  culling, texture/color/depth correction, vertex cache, projection, settings,
  and save/runahead support remain open.

### Real 8 MiB contract (audited, not implemented)

- The old `d1bfb6f0` “8mb support” commit did not implement RAM expansion and
  must not be reused. The coherent target is a fixed 8 MiB backing with a
  session-immutable live geometry: retail aliases physical RAM through
  `phys & 0x001FFFFF`; expanded mode uniquely decodes `0x00000000..0x007FFFFF`.
  KSEG aliases and the `0x00F00000` DMA aperture keep precedence.
- CPU, DMA, dirty-code, overlay, cosim/digest, PGXP, state, rewind, netplay and
  cache identity must adopt that one contract together. Start fail-closed for
  unsupported netplay/Beetle/cosim combinations; do not ship a hybrid map.

### Verification receipts and harness finding

- Current runtime and oracle build successfully. All 40 enabled runtime tests
  pass; the three upstream-disabled tests remain unclaimed:
  `sio_dualshock_rumble`, `spu_fidelity`, and `sio_card_protocol`.
- Focused full-function emitter, all 240 structural forms, recompiler patch,
  PGXP, transactional-state, and event-boundary tests pass.
- The overlay shim ABI compile passes with the bundled w64devkit GCC. Two test
  scripts previously forced `C:\msys64\mingw64\bin\gcc.exe`, which hangs in
  cc1 on this PC despite consuming no CPU. They now honor `CC`/PATH before that
  fallback. The much larger `aot_overlay_discovery` fixture still deadlocked in
  a bounded Windows process/lock case and was stopped by exact test PID; do not
  represent the entire 53-test recompiler suite as passing.

### Next concrete step

1. Produce a graceful-exit instrumented profile of fresh-boot FMV with the
   exact v3 Release feature set and a unique debug port; leave v2 PID 4316 alone.
2. Use the profile to introduce a cheap, explicit generated-branch poll gate
   (or an equally general AOT call/dispatch optimization), while the full IRQ
   path remains mandatory whenever a device/software IRQ, host request, or
   periodic maintenance boundary can be observable.
3. Prove instruction-boundary/event equivalence in focused tests, regenerate
   all game C, build a separate v4 candidate, and require fresh FMV throughput
   near the 99.493634-Hz target before any interactive launch or promotion.

## Second-pass architecture audit correction (2026-08-23 06:25 PDT)

The post-review v3 profile identified real hot code, but its comparison against
v2 was not controlled: v2 starts with six codegen-10 overlay DLLs indexed, while
v3's codegen-11/ABI identity invalidated that cache and starts with zero. Do not
attribute the v2/v3 delta to one runtime change until both candidates use the
same cache state and workload. This does not explain the whole deficit: v2's
own live log shows its opening FMV and many replay intervals at about 50-57 Hz.

The live v2 process remains PID 4316 and was not disturbed. It currently
saturates one logical CPU (100% of one core) while total machine CPU is about
9% and total idle is about 90%. Across 3,653 telemetry intervals, 2,828 were
below 60 Hz and 676 were at least 95 Hz. Exact 99.5-Hz stretches correlate with
the guarded title idle compactor firing; heavy stretches with zero idle skips
fall to roughly 50-57 Hz. This independently proves a single-thread guest-host
throughput ceiling, not a system-wide load or CD/SPU clock-domain drift.

The flat CPS trampoline remains a correctness requirement: restoring recursive
native guest calls would revive the proven unbounded host-stack failure. The
performance defect is the generic routing wrapped around that flat model:

- a normal clean game transfer can miss-search the 13,015-entry BIOS table,
  enter `dirty_ram_dispatch`, then binary-search the 23,336-entry game table;
- every successful game dispatch re-validates its exact emitted instruction
  ranges with `memcmp`, even when no executable write occurred since the last
  validation;
- `fntrace_record` unconditionally writes a dispatch-tail record and performs
  other diagnostics on every trampoline iteration; Release also emits a
  volatile function-entry stamp and updates the last-store PC at every store;
- the Release build is `-O3` but has no whole-program LTO or native-CPU tuning,
  leaving cross-translation-unit interrupt/dispatch fast paths out of line.

The next performance work must therefore be a controlled product-hot-path
tranche, not another timing shortcut: same codegen/cache/workload A/B; early
game-text routing before BIOS search; generation-qualified cached code validity
instead of per-call `memcmp`; a direct/static dispatch index rather than two
binary searches; and compile-time removal of post-mortem rings/stamps from the
shipping profile. Preserve CPS flattening, exact raw/native clock conversion,
and architectural IRQ/event boundaries. Use low-overhead counters and a
separate v4 build; do not launch over or stop PID 4316.

Other goal boundaries remain unchanged: transactional v6 state loading is a
real correctness improvement (old v5 slot 2 is intentionally incompatible),
the PGXP work is only a provenance-correctness tranche and is not full PGXP,
actual 8 MiB RAM is still unimplemented, wide aspect/resolution acceptance is
unfinished, and licensed bezel/font/text assets are still absent.

## Deep performance review and corrected A/B (2026-08-23 07:45 PDT)

This review supersedes the earlier v4 performance attribution. It does not
promote a new binary and it does not advance to 8 MiB, full PGXP, widescreen,
or asset work. The current tranche remains CPU/runtime architecture.

### What was actually wrong

- The 900% setting was not fake. Every controlled v3/v4/v5 interval reports an
  exact `cpu_effective=9.000x`, while SPU remains independently near 44.1 kHz.
  The title was slow because the single host execution thread could not retire
  the required raw guest work before each PALx2 VBlank.
- v3's controlled fresh-boot/FMV receipt has 36 samples and a steady
  idle-active median of 51.7 FPS, 158.39 million raw guest cycles/s, and 17.60
  million native-equivalent cycles/s. This is a throughput ceiling, not a bad
  on-screen counter.
- The v4 dispatch tranche was valid and material: clean compiled-game routing
  precedes the BIOS-table/generic dirty fallback; positive text admission is
  qualified by exact watched-page generations; and product Release removes
  unconditional post-mortem dispatch/function-entry bookkeeping while
  retaining mods, translation, game-start, armed traces/parity, and crash
  freeze behavior. Exact byte validation still fails closed after relevant
  writes and after restore/image epoch changes.
- Twelve bounded v4 GDB samples during steady FMV all landed in statically
  generated game code rather than the overlay loader. The sampled hot functions
  repeatedly paid an outlined `psx_cyc_step` call per retired non-load guest
  instruction. The generated tree contains about 491,800 such source call sites.
  In `func_801647D8`, v4 emitted 36 timing-step calls. This per-instruction native
  call boundary was the next general architectural bottleneck.

### v5 timing-inline experiment

- Release generated game bodies only were compiled with a force-inlined
  constant-mask pipeline step. BIOS, ordinary runtime/debug/oracle/cosim TUs,
  and non-Release configurations retain the compact generic path; overlays opt
  in separately. Replay, conservative stepping, device service, local timing,
  overflow, IRQ/MMIO publication, restore, boot, rollback, and long-jump paths
  retain exact slow behavior.
- Final measured v5 machine code contains no call to `psx_cyc_step` in
  `func_801647D8`. Its exceptional branches retain calls to a TU-local slow
  fallback. The function grew from 0x1130 (4,400 bytes) to 0x3D10 (15,632
  bytes), and the PE grew from 43,930,799 bytes (41.90 MiB) to 118,253,359 bytes
  (112.78 MiB). v5 SHA-256 is
  `8DEBAF630F8E792180E6E065F8EBC623821FD317159BAD88D065D3D3E004121A`.
- Six canonical overlay shards built successfully under measured cache identity
  `cg11_0d67212f_gce5e5eff6_f0`: 6 built, 0 skipped, 0 failed. The current
  source hash is now `e655f7bf` after the integration-test fix below, so the
  measured `0d67212f` binary/cache are evidence only and must not be promoted.

### Corrected controlled results

- An agent-created v4 `--help` probe had been ignored by the executable and left
  PID 13928 displaying the launcher since 06:51. It averaged modest but real CPU
  use and contaminated the first v4 A/B. That exact stale help-launcher PID was
  stopped; no gameplay/replay process was touched. The previously protected v2
  PID 4316 disappeared independently before this review and was not stopped by
  the agent.
- The contaminated v4 run reported 75.6 FPS steady median. A clean-host repeat
  instead reports 82.0 FPS, p05 76.9, p95 87.5, and 251.22 million raw guest
  cycles/s. Its log SHA-256 is
  `1F230457690352ACF8EEC40A7D16C18C85418E4031006472D4701315CD3AB62F`.
- Two clean v5 repeats are stable: steady medians 94.2 and 94.6 FPS, p05
  89.5/88.9, p95 98.3/98.9, raw medians 288.60/289.83 million cycles/s, exact
  9.000x CPU, and 44,100 Hz SPU median. Log SHA-256 values are
  `B9C4C86E081A15BA6EB4C1575E9863DD0D638A16E1F9F4EE543EFCA056D15DCA`
  and `DB011265107E29738668AFEA016ACED45584F48A083DFF8F398E7C2696CF1BAB`.
- Therefore the fair v5-over-v4 improvement is 15.4%, not the initially claimed
  25.1%. It is real and reproducible, but 39-40 of 44 v5 samples remain below
  the 99.493634-Hz target. A 2.69x PE-size increase for 15.4% while still missing
  full speed is an architectural proof, not a finished solution.

### Review-found integration defect and verification

- The first full test build exposed that the TU-local slow timing fallback was
  defined in the public header even for ordinary non-generated test/runtime TUs.
  At `-O0`, the unused static body still pulled the complete timing globals into
  intentionally narrow fntrace harnesses. The fallback is now emitted only for
  `PSX_GAME_GENERATED_FAST_CYC`; ordinary TUs call the compact generic helper.
  The timing harness is explicitly compiled with the generated-fast define so
  it continues to exercise the real product fast branch.
- Runtime and oracle now build successfully. All 43 enabled runtime tests pass;
  the same three hardware/protocol tests remain explicitly disabled and
  unclaimed. Six focused recompiler/codegen tests pass, and all six telemetry
  parser tests pass. The owned submodule diff passes `git diff --check`; the root
  check still reports an unrelated pre-existing blank line in root
  `CMakeLists.txt`, which was preserved.

### Review verdict and next concrete step

- Preserve the v4 dispatch/generation-cache/shipping-diagnostic work. It is
  fail-closed, independently useful, and accounts for a large clean improvement
  over v3 without changing CPU/device semantics.
- Do not promote v5 as the final timing architecture. Keep it as proof that the
  per-instruction outlined pipeline call is real overhead. The next iteration
  must retain the call-boundary win while sharply reducing duplicated cold
  paths and text growth—prefer emitter-level straight-line/basic-block timing
  aggregation or a provably safe function-scope fast/slow specialization over
  another global clock shortcut. Re-profile the clean v5 workload first so the
  remaining samples distinguish pipeline bookkeeping, I-cache pressure,
  instruction fetch checks, interrupt polling, and memory helpers.
- Require a same-host/same-cache 45-second fresh-boot receipt with steady p05 at
  or above the 99.493634-Hz target before interactive promotion. Then use the
  latest replay state only as an additional start/stop workload for race/CDDA
  validation; it is never a goal blocker.

## Authoritative deep retrospective (2026-08-23)

The owner requested a stop-and-review before further product work. The full
review is preserved in `docs/DEEP_RETROSPECTIVE_2026-08-23.md` and supersedes
the optimistic next-step ordering above.

- There is no promotable remaster binary. Measured v5 is older codegen
  `0d67212f`; committed source is `e655f7bf`. v5 misses target in 39-40/44
  windows and expands `.text` 3.244x.
- `Game FPS` is real wall-time VBlank throughput. `cpu_effective=9.000x` is an
  accounting ratio, while SPU/CDDA effective rates are simulated-time rates;
  current telemetry cannot prove audible wall speed or underrun freedom.
- The strongest next performance experiment is O(1), epoch-qualified direct
  clean dispatch. The current "cached" path still binary-searches 23,336
  entries and rescans covered pages on every dispatch; that family accounts for
  at least 18.35% of stable v5 stack samples.
- A bounded uncommitted compact timing-run prototype passes focused tests and
  bounds shared helper `.text` to 2,304 bytes plus at most 1.97 MiB of mask
  data. It is a later isolated A/B candidate, not the first next change.
- Current PGXP is neither FULL nor MAX and has concrete tolerance, signed-shift,
  FIFO, NCLIP, external-write provenance, cache/default, 2D, and feature gaps.
  Keep it disabled until the clean-room correctness sequence in the review.
- Coherent 8 MiB, wide-resolution acceptance, licensed bezel/font/text assets,
  wall-audio telemetry, and integrated current-source verification remain open.
- The replay is a reusable workload only. It must never pause or block the
  active goal.

Next implementation order: finish the isolated benchmark/audio contract;
measure a source-matching baseline; implement and prove O(1) direct dispatch;
then isolate branch/I-cache/compact-timing work as needed before 8 MiB, PGXP,
wide presentation, assets, and final integrated acceptance.

## Post-retrospective integration receipts (2026-08-23 15:30 PDT)

- The contamination-proof benchmark runner now rejects short/natural failures,
  guards the exact image immediately before launch, owns and stops only its
  child, uses fresh cards, and records binary/source/cache/host identities. Its
  guarded three-second v4 smoke was accepted with 2/2 required windows and an
  unchanged cache. All 15 diagnostics tests pass.
- Runtime telemetry now separates simulated SPU/CDDA cadence, wall generation,
  callback-consumed versus enqueue-produced host frames, queue health,
  underruns/overflows, callback starvation, and drift. Receipts explicitly do
  not claim software callback consumption proves physical DAC delivery.
- The unpromoted direct-dispatch experiment emits a bounded two-level O(1) map.
  WipEout regeneration contains 23,336 entries on 172 pages; exhaustive checks
  cover all 524,288 aligned retail-RAM words and all three segment aliases. The
  unchanged validity hit is one epoch comparison. Hot mutable data sharing an
  armed code page can still cause global invalidation; exported churn counters
  are not yet wired into benchmark receipts, so no performance claim exists.
- The current integrated runtime/oracle tree builds with pinned w64devkit. All
  44 enabled CTests pass; `sio_dualshock_rumble`, `spu_fidelity`, and
  `sio_card_protocol` remain disabled and unclaimed.
- The owner reported a `cc1.exe` modal for missing `libisl-23.dll`. Current
  build caches and all three active work lanes used w64devkit/Clang, not MSYS.
  Two legacy test-only hard-coded MSYS GCC fallbacks could produce exactly that
  failure when MSYS `bin` was absent from `PATH`; those fallbacks were removed.
  The overlay shim compile passes with explicitly pinned w64devkit. Never add
  MSYS to `PATH` as a workaround or use its compiler in this lane.

The authoritative retrospective verdict is unchanged: none of these focused
receipts is a source-matching product benchmark or a promotable game binary.

## Reviewed foundation ready for source-matched measurement (2026-08-23 16:05 PDT)

- Direct-dispatch churn is now emitted in every clock record as reset-safe
  `text_watched_writes` and `text_exact_revalidations` deltas, and the receipt
  reports their totals, distributions, and ratio without treating activity as
  proof of either speed or correctness.
- The benchmark rejects any overlay-cache content or namespace change during
  the timed interval. This closes the remaining shader-compilation-like
  contamination hole: the exact cache must be precompiled and stable.
- Review found and fixed the compact timing emitter's address-zero sentinel;
  valid KUSEG code at physical zero now starts a run through explicit state.
- The prior blank `0xC000001D` invalid-config failure was an uncaught C++
  exception hitting w64devkit's trap terminate handler. `psxrecomp-game` now
  has a top-level exception boundary and returns 2 with the loader diagnostic.
  This is separate from the removed legacy MSYS GCC fallback that caused the
  owner's earlier missing-`libisl-23.dll` modal.
- The integrated pinned-w64devkit runtime/oracle build succeeds. All 44 enabled
  CTests pass; the same three named hardware/protocol tests remain disabled and
  unclaimed. The reachable/direct-map/timing-run/overlay-shim/dirty-text focused
  checks pass, as do all 19 root diagnostic tests.

Next concrete step: commit and push this reviewed measurement/dispatch/timing
foundation, regenerate WipEout from that exact submodule commit, build a fresh
product directory and precompile its exact overlay cache, then require two
accepted 45-second fresh-boot receipts before any interactive promotion.

## Source-matched v6 verdict and timing A/B gate (2026-08-23 16:50 PDT)

- Committed foundation `010975f` / submodule `45461852` regenerated 201 game
  shards and 23,336 direct entries with codegen hash `88e034e5`. The fresh
  self-contained binary is 63,522,621 bytes, SHA-256
  `5CA5DABBC774E601F4EFCB253DA24E056E5D4485636E77323B866A6A84230161`.
- All six captured overlay regions compiled with the absolute pinned
  w64devkit GCC into the exact `cg11_88e034e5_gce5e5eff6_f0` namespace.
  A first run was correctly rejected because startup added the zero-byte
  cache audit marker. After that deliberate warm-up, the cache remained
  content-identical for both accepted measurements.
- Two accepted 45-second fresh-boot receipts contain 44 windows each:
  - source-matched-2: p05 88.505 Hz, median 94.7 Hz, raw p05 271.153M/s.
  - source-matched-3: p05 89.035 Hz, median 94.9 Hz, raw p05 272.777M/s.
- Both runs report zero watched-text writes and 815 exact validations, almost
  entirely the one-time startup admission wave. Global epoch churn therefore
  does not explain the miss. The result reproduces v5 rather than improving
  it and is not promotable.
- Timing-run aggregation now has its own `PSX_GAME_TIMING_RUNS` CMake gate,
  conservative default OFF. The same generated source can therefore measure
  direct dispatch with ordinary forced-inline per-instruction timing versus
  the shared run helper without changing device semantics or other runtime
  code. All 44 enabled integrated tests and the focused timing codegen test
  pass with the gate.

Next concrete step: commit the A/B gate, regenerate its new source hash, build
the default-OFF direct-only candidate, warm its exact cache, and take two clean
45-second receipts. Only build the gate-ON sibling if the direct-only result
needs an exact same-source comparison; do not promote v6.

## Direct-only timing A/B preliminary result (2026-08-23 17:20 PDT)

- Root `2c928b8` / submodule `a25cbacd` regenerated the game with codegen hash
  `b27d22c7` and built a fresh Release candidate with
  `PSX_GAME_TIMING_RUNS=OFF`. Its build graph contains zero
  `PSX_GAME_GENERATED_TIMING_RUNS` definitions. The binary is 118,657,120
  bytes, SHA-256
  `463F28675931D54FC18ABD7ED3A276F3F8DD75739CB69E4DC58FDFD25D3B7A7E`.
- All six canonical overlays built successfully with pinned w64devkit GCC into
  exact namespace `cg11_b27d22c7_gce5e5eff6_f0`: 6 built, 0 skipped, 0
  failed. The warm-up was correctly rejected when startup created the ABI audit
  marker. Both measured runs retained an identical 27-file cache manifest.
- Two subsequent 45-second receipts were accepted with 44 windows each and
  explicitly record `PSX_GAME_TIMING_RUNS=OFF`:
  - direct-only-1: p05 73.035 Hz, median 95.1 Hz, raw p05 223.758M/s, raw
    median 291.358M/s.
  - direct-only-2: p05 81.875 Hz, median 99.85 Hz, raw p05 250.841M/s, raw
    median 305.911M/s.
- Both runs report zero watched-text writes and the same 815 startup exact
  validations as v6. Direct dispatch validity churn is not the cause of the
  lower tail. Both direct-only p05 results are below v6's 88.505/89.035 Hz,
  while the executable is 1.868x larger than v6's 63,522,621-byte compact-run
  binary. The higher second-run median does not compensate for the materially
  worse and more variable tail.
- This initially appeared to show that compact timing runs materially improve
  code locality/tail throughput. A same-source ON control and host-load audit
  below found that one OFF receipt and all new controls were contaminated by
  unrelated whole-PC CPU load. Only direct-only-2 remains within the corrected
  host-load envelope, so this is a preliminary direction, not a completed A/B
  verdict. The OFF candidate remains non-promotable. The fresh product build
  does not register CTest targets; this does not replace the already-recorded
  44/44 integrated runtime/oracle result for the same submodule source.

## Host-contamination correction and exact ON control (2026-08-23 17:35 PDT)

- The exact gate-ON sibling records `PSX_GAME_TIMING_RUNS=ON` and applies
  `PSX_GAME_GENERATED_TIMING_RUNS` to all 201 generated shards. It is
  63,522,621 bytes, SHA-256
  `E43BCB85234555A5227EEA2E5A0A5B844513B47ACFA53D3D9062823FCEA8D13F`.
  Six overlays built 6/6 into exact namespace
  `cg11_b27d22c7_gce5e5eff6_f0`, and its cache stayed unchanged.
- PE section comparison proves v6 and this explicit-ON binary have identical
  section sizes and unwind/function layout. Their 43,400,848-byte `.text`
  sections differ in only 40 bytes across ten four-byte ranges: embedded copies
  of codegen hash `88e034e5` changing to `b27d22c7`. There is no timing or
  dispatch algorithm difference that can explain a large speed change.
- Nevertheless, explicit-ON measurements fell to p05/median 67.530/84.45 and
  62.575/69.9 Hz. A same-moment rerun of the old v6 binary also fell to
  72.905/78.05 Hz. Average total host load was 2.568, 8.557, and 6.681 logical
  cores respectively, versus 2.256/2.099 during the two original good v6 runs.
- One bounded five-second process delta identified active unrelated work:
  EasyAudioEncoder used 0.657 core, two Plex Transcoder processes used
  0.554/0.511 core, Plex Media Server used 0.134 core, and ChatGPT processes
  used about 0.28 core combined. No external process was stopped or modified.
- The benchmark now defaults to rejecting total average host load above 2.5
  logical cores, including the benchmark child, and rejects unavailable load
  measurements. This threshold retains both original good v6 receipts but
  rejects direct-only-1, both new ON receipts, and the same-moment v6 control.
  All 22 diagnostics tests pass.

Next concrete step: after unrelated Plex/transcode load naturally subsides,
take an alternating OFF/ON/OFF/ON series under the new host-load gate. Until
then, do static/binary profiling that cannot be biased by host load. Keep all
A/B binaries as evidence only; do not promote or interactively launch either
until a same-cache p05 reaches the 99.493634-Hz target.

## Generated I-cache pilot and child-isolated host load (2026-08-23 18:15 PDT)

- A bounded generated I-cache-hit pilot inlined the literal-address resident
  tag check at every static fetch leader. Runtime/oracle correctness remained
  green, but the resulting v9 executable grew from 63,522,621 to 77,303,482
  bytes. `.text` grew from `0x02963e90` to `0x03687cd0` (about 13.8 MB). Its
  exact overlays built 6/6 under `cg11_758cca07_gce5e5eff6_f0`. A rejected
  diagnostic run reached only p05/median 66.385/78.1 Hz while unrelated host
  work averaged 4.944 total cores. This whole-program form is ruled out because
  its native instruction-footprint cost is architectural, irrespective of the
  contaminated dynamic sample.
- A selective v10 pilot then inlined only 55 fetch sites in the two profiled
  leaves `0x801182F4` and `0x801647D8`; the other roughly 159,500 source sites
  retained the shared wrapper. Its executable was 63,529,013 bytes, only 6,392
  bytes above the explicit-ON control, with `.text` up only 6,192 bytes. All six
  overlays built under `cg11_2453561e_gce5e5eff6_f0`. Receipt
  `20260823-170836-430254900-hot-icache-external-load-2.receipt.json` was accepted:
  p05 72.1 Hz, median 87.95 Hz, raw p05 220.893M/s, raw median 269.453M/s,
  exact CPU ratio 9.0x, zero watched writes, 815 exact validations, and unchanged
  cache. It still trails the established clean v6 p05/median 88.505/94.7 and
  89.035/94.9 receipts. Host volatility prevented an accepted adjacent v8
  control. Therefore no I-cache inline form is promoted; the submodule is
  restored byte-for-byte to the parent of `de5406b8` for all owned files.
- Review found that the host-contamination gate counted the benchmark child as
  contamination. That creates selection bias: a candidate can improve work per
  window yet fail merely because its owned process used more CPU. The runner
  now reads the exact owned child CPU time (Windows `GetProcessTimes`, POSIX
  waited-child usage), records total/child/external busy cores separately, and
  gates only external work. The conservative default is 1.5 external cores,
  replacing 2.5 total cores so the earlier clean/dirty classification remains
  strict. The live Windows probe returned 0.34375 CPU-seconds for its owned
  child, and all 11 runner unit tests pass.
- The corrected accepted v10 receipt separated 2.406 total cores into 1.004
  owned-child and 1.402 external cores. Rejected paired receipts now state the
  external component explicitly; Plex/transcoding and ChatGPT are never stopped
  or modified. A fresh compact-runtime/oracle rebuild passes all 44 enabled
  tests; the same three hardware/protocol tests remain disabled and unclaimed.
  Focused reachable-discovery and timing-codegen tests pass. A broader
  direct-dispatch CTest invocation exposed four pre-existing source-policy
  failures (`launcher_vulkan_option`, `mod_load_acceleration`, `release_zip`,
  `vk_present_wait_stage`); its long overlay-discovery test was stopped after a
  declared bound. None overlaps the reverted I-cache files or runner change.

Submodule `4f0e42a2` is the pushed compact-codegen revert. The current generated
game source is restored to hash `b27d22c7`. Next concrete step: resume the
alternating timing ON/OFF measurements only during an external-load window at
or below 1.5 cores. Do not reintroduce generated I-cache inlining without a new
profile and an accepted paired receipt that beats the clean v6 lower tail.

## Coherent 8 MiB product build and first remaster integration (2026-08-23 10:46 PDT)

- The previous `d1bfb6f0` "8mb support" implication was rejected after a full
  address-path audit. The replacement contract is now explicit and coherent:
  one fixed 8 MiB backing allocation, a build/session-immutable 2 or 8 MiB
  decode geometry, retail four-way 2 MiB folding, unique expanded addresses,
  and the same geometry across CPU loads/stores, DMA, dirty-RAM execution,
  overlay capture/loading, boot state, savestate bounds, debug hashing,
  generated dispatch, PGXP provenance, and startup configuration validation.
  Unsupported expanded netplay/cosim combinations fail closed.
- `game.toml` and the product CMake target both select 8 MiB. Regenerated
  `SCES_028.45_dispatch.c` rejects `phys >= 0x800000`; its exhaustive lookup
  verifier passed with 23,336 entries, 172 populated pages, three aliases per
  entry, and 2,073,816 expected full-range misses. The recompiler parser and
  generated map enforce the same 2/8 MiB choice.
- All 46 enabled runtime tests passed. Retail and expanded memory-geometry
  tests, DMA aperture tests, capture/overlay structural guards, interpreter
  hot-path guards, four focused recompiler/dispatch tests, and the exhaustive
  generated lookup all passed. The same three disabled hardware/protocol tests
  remain unclaimed.
- The first full Release product build completed for both stock and PGXP with
  `PSX_MAIN_RAM_BYTES=0x00800000u`, `cpu_overclock=900`, and
  `pal_video_clock_multiplier=2`. The stock binary is 63,544,819 bytes with
  SHA-256 `B746DF630477B6BEFC4FC72A72B24F09E88371B18457EE3F636B20DB3D411573`;
  the PGXP sibling is 72,482,208 bytes with SHA-256
  `87E67077FE24B37088F7B4DC4D299B781B0087FC24A23586B9558D164214457B`.
  These hashes identify the first integration build, not the final accepted
  release.
- The stock integration binary was launched with `--no-launcher` and the
  authenticated retail BIOS as PID 33668. It was deliberately left running.
  The heartbeat advanced beyond 3,600 VBlanks with no fatal record and sampled
  close to the configured PALx2 rate. This is a live boot/progress smoke only;
  the user still owns FMV/card/countdown/race/CDDA/geometry acceptance.
- The first PGXP correctness tranche now has target-sized 2/8 MiB provenance,
  symmetric tolerance, UB-safe signed coordinate conversion, reference-like
  defaults, external-write invalidation primitives, and exact MTC2/LWC2 SXY
  FIFO propagation. Retail and expanded PGXP tests pass, but **FULL/MAX PGXP is
  still open**: raw-writer/DMA continuity, savestate/rewind continuity, precise
  NCLIP, broader propagation, lines/polylines, perspective color/depth, 2D and
  transparent-depth policy, full settings, and live visual proof remain.
- The live WipEout launcher now owns 4:3, 16:9, 21:9, and 32:9; 24 curated
  resolutions; and 44 valid 60/100 Hz resolution pairs. 8K-class modes are
  capped at 60 Hz, refresh remains presentation-only, and selected dimensions
  reach SDL windowed/exclusive-fullscreen creation. Focused parser/catalog and
  title-ownership tests pass. Hardware/visual validation remains open.
- The shared launcher stages its existing licensed fonts and records their
  notices. A strict user-supplied HD-pack validator/receipt was prototyped in
  historical `clean-aot`, but the shipping PSX runtime does not yet consume it.
  No old bezel or HD texture/font payload was bundled because
  `assets/provenance/hd-asset-permission.json` still lacks a license grant.
  Do not claim live replacement hits or shipped bezels/HD text yet.

Durable commits for this tranche are psxrecomp `8ddd3843` and recomp-ui
`b81fcf8`. The next concrete step is to finish the alternate-directory relink
without disturbing PID 33668, commit/push the root submodule pointers and
8 MiB configuration, then collect the user's complete stock acceptance. In
parallel, finish the three disjoint PGXP continuity, rendering, and settings
tranches before launching the separate PGXP executable.

Push receipt: psxrecomp `8ddd3843` is published on
`mstan/psxrecomp:codex/wipeout3-upstream-audit`. Upstream recomp-ui rejected the
authenticated `tetrisgm` account with HTTP 403, so the required fork
`tetrisgm/recomp-ui` was created and `b81fcf8` was published on
`codex/wipeout3-remaster-controls-assets`; root `.gitmodules` now names that
reachable fork rather than leaving an uncloneable submodule pointer.

## Windows invalid-overlay probe UI correction (2026-08-23)

- The reported `python.exe - Bad Image` dialog naming a temporary
  `00010000_DEADBEEF.dll` was traced exactly to the overlay-discovery regression
  test. Its publication-race case deliberately writes `OLD-DLL`/`NEW-DLL`
  placeholders and then verifies that runtime-style coverage rejects the
  malformed winner. The rejection is expected and is not a game-process crash.
- `tools/compile_overlays.py` now wraps all candidate `ctypes.CDLL` validation
  mappings in the thread-local Windows
  `SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX` mode and immediately restores
  the prior mode. Invalid/stale cache candidates still fail closed, but no
  system-modal loader dialog can stall the bounded Python test or compilation
  tool. Python compilation and a direct malformed-DLL rejection probe pass.

## Live 50/100-Hz transition and replay fixtures (2026-08-23 11:49 PDT)

- The owner changed the running accepted stock process from the game's 50-Hz
  patch mode (which ran too fast under the runtime's PALx2 cadence) back to its
  100-Hz patch mode; the latter then ran very slowly during post-race replay.
  This is a useful discriminator: the 50-Hz content path is not a valid PALx2
  acceptance route, while the 100-Hz path exercises the required throughput.
- PID 33668 is still the stock executable
  `Wipeout_3_Special_Edition__Recompiled.exe`, not the `_pgxp.exe` variant.
  Therefore its current visuals cannot validate or invalidate the new FULL/MAX
  PGXP implementation, regardless of launcher/config PGXP selections.
- Three new retail-BIOS fixtures were preserved without touching the process:
  slot 0 at race start/post-race boundary, SHA-256
  `004E163C683B301E026FCD5022D832F3168B0DD3870C09046F9E0C1337E3B329`;
  slot 1 post-race, `75CCE1345186D697981F2F9DC4E861231CEE242E634E22A87227AD8C803F79DA`;
  slot 3 post-race, `771400741A93CB7E98680379FDD643ADE2D28C3DE6721945B4C80FBDDF369516`.
- Two stale Codex-owned read-only diagnostics were stopped; the game and all
  unrelated processes were left alone. A subsequent three-second sample showed
  the game using 1.099 CPU cores, total host CPU 15.4% of 16 logical processors,
  and a responsive window. All local build/sanitizer work was held while the
  owner observed the replay. The fixtures, not the live loop, are the durable
  reproduction route for the isolated candidate.

## PGXP visual-profile correction and HD save-state continuity (2026-08-23 14:22 PDT)

- The first visible PGXP/HD candidate was not a valid HD-pack visual test. It
  auto-loaded legacy slot 3, while this runtime discarded its Beetle-style
  texture-upload tracker on every full VRAM restore. The external pack was
  active and indexed all 292 keys, but no replacement could match until the
  scene uploaded the relevant source texture again.
- The HD tracker now has a bounded, versioned, little-endian state wire and is
  an optional boot/save-state section. The loader preflights counts, sizes,
  fragment geometry, overlap, serial uniqueness, and truncation before a
  transactional swap. Old states remain loadable; they explicitly restart the
  tracker from subsequent uploads. New states preserve partial-upload holes and
  surviving fragments, matching Beetle's continuity model.
- Focused `boot_state_transaction_test`, `hd_texture_pack_test`,
  `overlay_static_cmake_contract_test`, and
  `static_overlay_entry_index_test` passed 4/4. The HD test also passed against
  the real cue-sibling 292-key pack, including corrupt/truncated rejection and
  failed-load rollback behavior.
- The original `FULL/MAX` preset was too aggressive. DuckStation's official
  SCES-02845 database requires CPU propagation and exclusion of 2D polygons,
  but does not require vertex cache or preserve-projection. DuckStation leaves
  both latter options off by default and warns that unnecessary cache/CPU
  overrides can cause rendering errors. WipEout genuinely requires CPU mode;
  the cache does not. The corrected product profile therefore keeps geometry,
  texture correction, CPU mode, precise culling, and exclude-2D enabled; turns
  vertex cache and preserve-projection off; and continues to keep host
  color/depth/transparent-depth off after their observed flying/missing polygon
  failures.
- The isolated corrected executable is
  `build-remaster-pgxp-hdstate-20260823/Wipeout_3_Special_Edition__Recompiled_pgxp.exe`,
  SHA-256 `47AE915A08BE067EFD070C82BD4FFDF5C4B9543F98025523D50FCB73CB5FE153`.
  It is staged with the corrected `game.toml`, bezel, copied input bindings,
  isolated cards, and the existing validated overlay cache. It has not been
  launched because PID 28888 is still the owner's visible older candidate and
  must not be stopped or doubled up without an explicit handoff.

Next concrete step: once the owner has finished with or closed PID 28888,
launch the isolated corrected executable with `--no-launcher`, the retail BIOS,
and its own `candidate-saves`. A legacy state can prove PGXP geometry but not an
immediate HD hit; enter/restart a race so textures upload, confirm a first-hit
receipt, save a new state, reload it, and confirm a nonzero restored tracker and
the same replacement. If geometry still looks worse, run only the bounded
tolerance A/B (`-1.0` versus `0.5`) before considering preserve-projection; do
not re-enable vertex cache, color correction, or depth for this title.

## Corrected candidate live status and native shard repair (2026-08-23 14:37 PDT)

- The older visible candidate exited, so the corrected executable was launched
  as PID 4056 from `build-remaster-pgxp-hdstate-20260823` with isolated cards
  and the retail BIOS. It is responding. Startup telemetry proves CPU 900%,
  PAL×2 effective 99.493634 Hz at nominal 100 Hz, SPU 44.1 kHz, OpenGL, PGXP
  CPU on, vertex cache off, preserve-projection off, exclude-2D on, and host
  color/depth/transparent-depth off. The pack reports 292 unique keys and the
  bezel reports loaded.
- The copied old overlay cache was correctly rejected at startup: it carried
  `cg11_98645701_gc3d97ace3_f2`, while this executable expects
  `cg11_4e941aa0_gc3d97ace3_f2`. This mismatch explains the live title's
  interpreter-like 0.8x rate and must not be mistaken for a PGXP verdict.
- The prior capture manifest was copied only as compile input. A current
  `compile_overlays.py --check` rebuilt all five native-capable shards in a
  throwaway directory with the current recompiler and headers (3 data-only
  captures skipped, 0 failures). The five matching GCC DLLs and range files
  were then published into the corrected cache namespace. The running process
  scanned before those files existed, so it needs a clean restart to load them;
  it was left running to avoid another unexpected close.

Next concrete step: when the owner is ready to restart PID 4056, relaunch with
the same isolated directory (and a correctly quoted window-title argument),
confirm the startup audit reports `indexed=5` and no hash mismatch, then perform
the visual PGXP/HD race check. Do not judge the current 0.85x title as the
native-cache result.

## Replay miss-cache invalidation and single-instance launch guard (2026-08-24)

- The two-instance comparison ruled out host contention: one and two copies
  showed the same replay cadence. The bottleneck is guest emulation work.
- The replay hot path was an unresolved dynamic-overlay PC (`0x00780688`).
  `memory.c` previously advanced the overlay code epoch for every write to a
  watched 4 KiB page, including replay data writes sharing that page. That
  invalidated the lazy negative-dispatch cache continuously and repeatedly
  rescanned the manifest/range index.
- `memory.c` now keeps a second-level 4-byte instruction-word bitmap. Page
  generations and lazy-miss invalidation advance only when a write overlaps a
  registered instruction word; unrelated data writes remain on the cheap path.
  The existing direct negative cache then stays effective (`miss_cached=1`,
  `watched_gen=2` across the controlled replay sample).
- The old title counter reported simulated VBlank callbacks as “Game FPS”. The
  runtime now labels separate `Guest Hz`, `Present FPS`, and optional
  `Display FPS`; OpenGL present counting includes interpolation swaps.
- A guarded launcher helper, `tools/run_candidate_single.ps1`, kills only an
  existing process whose exact executable path matches the candidate, verifies
  zero remain, then launches one PID. It now passes launch-time environment
  variables explicitly. The correct save-state variable is `PSX_LOAD_SLOT`.
- Focused verification: `test_fps_telemetry_opt_in.py` passes; the PGXP
  candidate relinks successfully. Controlled slot-3 replay loaded successfully
  (`pc=0x80136B00`). The prior replay sample averaged 45.6 guest Hz; the fixed
  sample averaged 66.5 guest Hz (range 55.9–84.7 Hz) with the miss cache stable.
- Current intentional test process is one exact-path PID only. Kill it with the
  helper before any rebuild/relaunch; do not start a second candidate instance.

Next step: keep the single guarded slot-3 replay running long enough for a
visual check. If it still drops below the user's acceptable threshold, profile
the remaining emulation work (not host CPU contention) rather than adding more
one-off overlay fragments. A new user save state is useful only after reaching
the target race/replay with this build; the existing state remains loadable.

## Unified high-RAM CPS pilot (2026-08-24)

- The remaining replay defect is not the host: the high-RAM capture at
  `0x80780000` had dispatch evidence but no callable seed, so the compiler
  emitted isolated 14-byte interior DLLs. The hot interpreter PC remained
  `0x00780688`.
- `compile_overlays.py` now permits a narrowly gated orphan-promotion pass for
  a no-root capture that has explicit `dispatch_entry_pcs`. It promotes only
  those dispatch-proven PCs (never generic `executed_pcs`) into one CPS region
  bundle; established rooted/data regions keep the old conservative behavior.
- The cache gate treats this as a unified-capture demand: supplemental/orphan
  fragments cannot suppress creation of the canonical `<region>_<crc>.dll` and
  `.ranges` pair. A valid canonical pair alone can satisfy the demand.
- Offline pilots built successfully with the current recompiler and GCC:
  `0x80780000/0x326CD0D1` produced one validated CPS DLL with 2 functions,
  0 unknown targets, and 0 unsupported instructions; the richer `0x63E194BA`
  capture produced one 12-function bundle. The 326CD0D1 pair is staged in the
  guarded candidate cache.
- The current restored state resumes directly at the still-uncovered interior
  `0x00780688`, so it cannot exercise the new region entry before the next
  capture. A fresh user state saved while the replay is visibly slow will
  capture that exact dispatch demand; then compile it into the same canonical
  CPS bundle. No user state is required for the compiler pilot itself.

## Bounded hot-page promotion and gameplay-speed interpretation (2026-08-24)

- Runtime capture now bridges resumed interior dispatches without promoting
  arbitrary executed data: it promotes the hot PC plus a bounded set of
  dispatch-table roots from the same page, and can promote one contiguous
  execution run of at most 256 instruction words. Disconnected execution
  islands remain excluded.
- Capture history is merged by exact `(load_addr, size, bytes)` image before
  compilation, so repeated save-state runs contribute roots to the same
  canonical RAM image instead of producing an unbounded pile of fragments.
- The F5 pilot (`0x800F5000`, CRC `0x8C411A77`) compiled to 104 native
  functions with zero unsupported instructions and zero unknown targets. The
  780 pilot (`0x80780000`, CRC `0x8B293C32`) compiled to 8 native functions
  with the same zero-error audit and is installed alongside the historical
  780 bundles.
- A clean single-instance verification confirmed the candidate starts with
  `CPU overclock 900%` / `cpu_effective=9.000x`. Its restored replay still
  reports variable guest cadence because the diagnostic counter measures
  simulated VBlank work, not the player's perceived motion; the user reports
  the race itself feels close to full speed when CDAudio and that counter are
  ignored. This is not a claim that every dynamic block is now native or that
  a 900% gameplay result is fully proven.
- The exact candidate process was closed after the run and verified at zero
  matching processes. Future tests must use the guarded launcher and preserve
  this single-instance invariant.

## Savestate UI restore and clean 400% handoff (2026-08-24)

- The race-start state supplied by the owner is preserved at
  `saves/scph1001/state_80162474_slot00.pst` (backend slot 1) and copied into
  the isolated `analysis-saves-race` card directory. No new state is required
  for the next playable check.
- `gpu_snapshot_read()` now restores renderer texture-window, draw-offset, and
  mask mirrors, then clears host-only widescreen/UI tags, prepass ranks,
  backdrop phase, framebuffer observations, and split-screen latches. These are
  rebuilt from the restored frame instead of leaking the abandoned frame into
  menu/HUD primitives. The candidate PGXP runtime relinked successfully.
- A focused verification pass is green: runtime cycle boundaries, cycle batch,
  boot-state transaction (3/3), PGXP/render-policy, PGXP, HD texture, bezel,
  and boot-state tests (5/5), plus interpreter/dirty-text/static-index guards.
- A new fast-path guard avoids PGXP provenance lookups when all corrections are
  disabled. This preserves the native PS1 path for the explicit PGXP-off A/B;
  the change is built but the remaining low-RAM replay path still needs a
  native shard, so it is not presented as the final performance fix.
- The sibling `build-remaster-pgxp-hdstate-20260823/game.toml` is a deliberate
  400% profile but uses relative media/output paths. Launching it by default
  produced ~2 Hz on the race state; explicitly selecting
  `analysis-nohd/game.toml` (absolute disc/seed/output paths, HD pack off)
  reaches 56–68 guest Hz with PGXP on and rewind off. The known-profile process
  is currently the sole running candidate (PID 15136); do not start another
  instance. Its logs are `user-ready-knownprofile.out.log` and `.err.log`.
- A stale overlay compiler from the old `wipeout3se` worktree was found
  consuming CPU and stopped; no unrelated applications were terminated. Any
  future run must check for that exact old-worktree compiler before judging
  cadence.

Next concrete step: the owner should test the already-running known-profile
  window and, if the restored race is visually correct, drive/save one race.
  If UI is still absent, capture one screenshot from this process before any
  further rebuild. For performance, trace why the explicit profile's
  `0x00174A08` low-RAM dispatch remains interpreted; do not publish the failed
  mixed-code `force-interior` fragment or alias stale cache shards.

## Rebuilt 400% race candidate with rewind disabled (2026-08-24)

- The current candidate is the freshly rebuilt
  `build-remaster-pgxp-hdstate-20260823/Wipeout_3_Special_Edition__Recompiled_pgxp.exe`.
  The game C was regenerated with the current recompiler and the build was
  reconfigured so its shard list matches the generated output (192 shards).
- The recompiler now shares a reachable-CFG closure between normal and alias
  emission and emits a regression fixture for the A08/A44 mixed code/data
  shape. Its focused `recompiler_patch_test` passed, but the generated
  `func_80174A08` is still an unknown-dispatch stub; this remains an open
  low-RAM coverage issue, not a claimed fix.
- The staged runtime profile is intentionally 400% CPU, PALx2/100 Hz, PGXP on,
  and HD texture pack off. Controlled A/B already proved that the current HD
  tracker path collapses the same restored state to roughly 2 Hz, while HD off
  stays around 56–68 guest Hz.
- Automatic rewind is explicitly disabled with `PSX_REWIND=0`. Previous replay
  benchmarks were contaminated by the default full raw snapshot ring (every
  4 VBlanks during FMV); do not compare those numbers to this run.
- Exactly one candidate is running (PID 14688) from the cleanroom path. It
  loaded the owner's slot-1 race state successfully (`pc=0x801700C4`) and has
  remained responsive at roughly 58–65 guest Hz in the first intervals. Logs:
  `build-remaster-pgxp-hdstate-20260823/user-ready-regenerated.out.log` and
  `.err.log`.
- The owner can now play the loaded race and save a fresh race-start state and
  a post-race/replay state if reachable. No new state is required merely to
  start this check; do not launch a second instance while it is running.

## Rewind default corrected (2026-08-24)

- `psx_rewind.c` now treats rewind as opt-in: a plain launch has no raw snapshot
  ring, while `PSX_REWIND=1` explicitly enables the interactive feature. The
  previous default captured the complete machine every few VBlanks (FMV mode
  used four), contaminating FMV/menu/race/replay performance even when rewind
  was never opened. `PSX_REWIND=0` remains an explicit disable.
- A separate PGXP-off 400%/PALx2/HD-off build is being compiled for a visual and
  UI A/B; the currently running PID 14688 remains untouched so the owner can
  play the existing rewind-off candidate.

Next concrete step: use the fresh states from this rewind-off/HD-off build to
separate ordinary race performance from replay performance, then address the
remaining low-RAM native-dispatch and UI/HUD coverage issues from those exact
states. Keep process cleanup scoped to this executable path.

## PGXP-off controlled A/B is running (2026-08-24)

- A separate `build-remaster-pgxp-off-20260824` executable was rebuilt from the
  current source with 400% CPU, PALx2, HD textures off, and all PGXP geometry/
  perspective/culling corrections off. Its staged `game.toml` hash matches the
  deliberate A/B input exactly; SCPH-1001 was staged explicitly.
- The previous candidate was stopped by exact executable path before launch.
  There is currently one WipEout process only: the new PGXP-off executable.
  Launch environment includes `PSX_REWIND=0`, `PSX_DISABLE_HD_TEXTURE=1`,
  `PSX_RUNTIME_PERF_DIAG=1`, and the GL presentation probe.
- The owner's old slot-1 state in `saves/scph1001` was found, but this fresh
  executable rejected it during section preflight after codegen/ABI migration.
  This is an input compatibility failure, not evidence about the A/B runtime.
  The process is therefore booted without a usable restored race state; the
  owner should make a fresh race-start state in this executable before judging
  gameplay or replay.
- Startup diagnostics are reaching the game path at roughly 80–95 guest Hz
  while the menu/boot loop runs, with rewind disabled and HD uploads off. The
  exact logs are `build-remaster-pgxp-off-20260824/user-slot1-off.out.log` and
  `.err.log`.

Next concrete step: the owner should play a race in this sole running window
and save a fresh race-start state (slot 1) plus a post-race/replay state if
reachable. Then use those exact states for the CPU/overlay/UI investigation;
do not reuse the rejected pre-A/B state or start a second process.

## Current clean run and remaining blockers (2026-08-24)

- The PGXP-off target was relinked after removing a runtime block that
  unconditionally forced WipEout PGXP controls on after `game.toml` was read.
  The explicit all-false PGXP A/B is now actually honored; this is a bounded
  correctness fix, not a performance claim.
- The old process was stopped by exact executable path and the rebuilt
  `build-remaster-pgxp-off-20260824/Wipeout_3_Special_Edition__Recompiled_pgxp.exe`
  is the sole running WipEout process (PID 15392). It was launched with
  400%/PALx2, HD textures disabled, rewind disabled, and no stale save-state
  slot selected. The owner must create a new slot-1 race state in this build;
  the prior 900% state is incompatible and was deliberately not loaded.
- Missing race/menu HUD elements and the reported 3 FPS in-race view remain
  open defects. They are now part of the same acceptance gate as gameplay
  cadence: capture fresh race and replay states, then inspect UI primitive
  coverage and CPU/overlay invalidation on those exact states.
- The current prepared disc is the plain PAL patch (`pal_track01`), not the
  requested `pal8` enhanced 8 MB patch. Applying the archived `pal8` delta to
  this already-patched image fails its source hash check. A legal original
  unpatched Track01 is still needed before claiming a true PAL8 baseline;
  never overwrite the current media with a mismatched delta.

## Guarded playable handoff without stale state (2026-08-24)

- The previous candidate process was no longer present when checked; no second
  WipEout instance is running. The deliberate PGXP-off 400% executable was
  relaunched through `tools/run_candidate_single.ps1` as PID 35352.
- This launch uses the staged `build-remaster-pgxp-off-20260824/game.toml`,
  PALx2/400%, HD textures off, all PGXP corrections off, `PSX_REWIND=0`, and
  no forced save slot. Logs are
  `build-remaster-pgxp-off-20260824/user-clean-400.out.log` and `.err.log`.
- The old slot-1 file (`state_80162474_slot01.pst`, ABI 22 / codegen
  `0x42326476`) fails section preflight under the current ABI and is not a
  valid measurement input. Slot 0 loads through migration but leaves a
  different, old replay path; neither should be used as the new baseline.
- Clean boot reaches the game path without the raw rewind ring: early cadence
  is ~90--100 guest Hz with zero interpreter work in the BIOS/menu loop. The
  owner should play this sole window and save fresh slot-1 race-start and,
  if reachable, post-race/replay states from this exact executable.
- The UI audit found no renderer/present failure: OpenGL is initialized,
  GP0 packets execute, and the post-load probe records 237 swaps/63 intentional
  skips over 300 VBlanks. The `GL upload/draw` telemetry is upload-only and is
  not a scene-draw counter. Missing HUD/menu primitives therefore remain an
  upstream CPU/dirty-code coverage issue to diagnose from fresh states.

## Cache-aligned playable handoff (2026-08-24)

- Rebuilt `psxrecomp/recompiler/build/psxrecomp-game.exe` from the current
  source. Its `--codegen-hash` is `b24a140a`, matching
  `psxrecomp/runtime/include/overlay_codegen_hash.h`.
- Ran the historical overlay capture through the current compiler in a
  throwaway preflight (`built OK: 2, failed: 0`), then published the same two
  verified shards into the active candidate cache. No old `4e941aa0` or
  quarantined shards were copied.
- Relaunched the sole guarded candidate as PID 34528 with the active
  `cg11_b24a140a_gc3d97ace3_f2` cache, PALx2/400%, PGXP/HD off, and
  `PSX_REWIND=0`. Startup audit reports `indexed=2`, `status=ok`.
- This is the only process to test now. The owner should play the race and
  save a fresh slot-1 race-start state in this exact process; if the race can
  be completed, save a second post-race/replay state as well. Old slot files
  remain invalid because they contain a different 100 Hz patched code image.
- Acceptance still includes the missing menu/race UI, stable cadence, no
  flying geometry, and truthful telemetry. Do not call this a final baseline
  until fresh-state logs show the actual race/replay hot PCs served by native
  overlays or a proven cause is fixed.

## CPU/scheduler audit conclusion (2026-08-24)

- The HLE scheduler is outer-loop based: a guest yield saves the TCB and
  returns once to the scheduler, which restores the target and dispatches. It
  does not perform a scheduler operation per instruction. LLE save-state loads
  are explicitly rejected because cross-fiber restore is unsafe. Keep HLE
  enabled while measuring.
- The old 3--20 FPS save-state runs are dominated by dirty interpreter work at
  `0x80170E04`, not by HLE/LLE switching or the GPU. The generated native
  function exists, but the old state has ten instruction-word differences from
  the generated image (nine SRA and one SRL words) inside that function. Text
  validation correctly rejects it, so the whole function falls back to the
  interpreter. Never disable that guard or whitelist the address.
- The older replay traces also show a separate global overlay negative-cache
  invalidation storm; it remains a follow-up only after a fresh exact-code
  state is running. Do not tune device deadlines or the frame pacer from the
  mismatched-state traces.

## Fresh-state capture protocol (2026-08-24)

- The current PGXP-off shipping candidate has no debug TCP server and does not
  auto-compile overlays. Its runtime capture store is
  `build-remaster-pgxp-off-20260824/overlay_captures.json`; capture evidence is
  written on clean shutdown.
- UI slot 1 is backend `slot00.pst`; UI slot 2 is `slot01.pst`, under the
  launch-selected `saves/scph1001` directory. A fresh state must be made by
  PID 34528, not imported from the old 100 Hz build.
- After fresh states exist, relaunch only through the exact-path guard with
  `PSX_LOAD_SLOT=0` or `1`, `PSX_REWIND=0`, and a fresh capture path. Run the
  capture state through race/replay, close normally so the manifest is flushed,
  then run `compile_overlays.py --check` followed by the real compile using the
  rebuilt `b24a140a` recompiler. Never publish stale history shards by hand.

## Fresh owner states and replay coverage (2026-08-24 12:00)

- The owner supplied fresh states in the plain PAL/8 MiB 400% lane. Slot 1
  (`state_80162474_slot01.pst`, SHA256
  `8472F6A89268EB4DEDC22249B8AE41C4AE1906656066252057D6C2E896B1BB2A`)
  restores at `pc=0x8015C404`; slot 2 (SHA256
  `E9C2A5EFE5B796C2B44B28FC49BFA710BE88FF1C7DA26C78DACE8A60073DD554`)
  restores at `pc=0x801722B0`.
- With the exact B24 executable/config, `PSX_REWIND=0`, HLE scheduler, and
  HD textures off, both states sustain `99.45--99.60 Hz`, SPU
  `44.0--44.1 kHz`, and zero audio underruns/overflows. A live slot-2 window
  visibly contains the position/time/lap HUD; the old 3 FPS result is not
  reproduced by these states.
- Clean shutdown of slot 2 flushed an exact replay capture for
  `0x80780000` with dispatch roots `0x80780688`, `0x80780998`, and
  `0x80780E44`. The current recompiler (`b24a140a`) passed `--check` and
  published `00780000_A5D657CE.dll/.ranges` (3 functions, 0 unsupported, 0
  unknown). A later run captured `0x807809AC`, producing
  `00780000_B9F810F3.dll/.ranges` (5 functions, same clean result).
- The low replay path likewise produced a canonical 18-function
  `00148000_59BBFF88.dll/.ranges` containing `0x80148920`, plus a bounded
  exact-demand orphan `00148000_4E580E7B.dll/.ranges` for `0x80148CF8`.
  These were generated through `compile_overlays.py`, never copied or hand
  edited. The cache now audits as `indexed=71`, tag
  `cg11_b24a140a_gc3d97ace3_f2`.
- The hot interpreter PC moves between nearby continuation entries as the
  restored replay progresses (`0x80780E44`, `0x807809AC`, `0x80148920`,
  `0x80148CF8`, then other neighboring tails), while native work remains
  roughly `520--535k` calls/5 s and interpreter work `19--25k` calls/5 s.
  This is bounded path-coverage fallout, not a pacer, GPU, or host-CPU
  failure. Do not admit all observed PCs: compile only exact captured roots or
  validated `--force-interior` demands.
- The sole active process, if left running for manual inspection, must be the
  exact `build-remaster-pgxp-off-20260824` executable. Stop it by exact path
  before any rebuild or cache replacement; never run a second WipEout instance.
- True PAL8 provenance remains a separate unfinished lane. Plain-PAL states,
  media, and shards cannot be reused as PAL8 evidence; fresh PAL8 states and a
  PAL8-specific capture/cache are still required before claiming that target.

## Rebuilt candidate and fresh-state verification (2026-08-24 12:15)

- The Release PGXP target initially failed to relink because GCC 16.1.0 has a
  deterministic internal compiler error in `psx_pgxp_fast_alu()` while compiling
  `generated/SCES_028.45_full_123.c` with fast cycles + PGXP at `-O2/-O3`.
  The bounded fix is source-specific `-O1` for that one generated TU in
  `psxrecomp/runtime/runtime.cmake`; the rest of the generated game and runtime
  remain at their normal Release optimization. The command now contains
  `-O3 ... -O1 -O1`, and `psx-runtime-pgxp` relinked successfully.
- The rebuilt executable is
  `build-remaster-pgxp-off-20260824/Wipeout_3_Special_Edition__Recompiled_pgxp.exe`
  (SHA256 `47A2D7E705D4BA14A26727E681491CC5C0F097025AB8A7549F6390A5B0AB4D21`).
  Its sibling config is SHA256
  `00D0092E904792F21E7D0F8C92E0205FBBF890794C70712046E5149D9576ED99` and is
  the deliberate PALx2/400% PGXP-off/HD-off A/B profile.
- The latest exact user-state capture for `0x80780A48` passed compiler
  preflight and real publication as `00780000_C8C2A692.dll/.ranges` under the
  active B24 cache (1 dispatch-root function, 0 unsupported/unknown).
- Using the rebuilt executable, explicit SCPH-1001 BIOS, the actual `saves`
  root, HLE scheduler, `PSX_REWIND=0`, and slots 1 and 2:
  - slot 1 loaded `pc=0x8015C404` and held `99.45--99.64 Hz`, SPU
    `44.0--44.1 kHz`, with zero audio underruns/overflows;
  - slot 2 loaded `pc=0x801722B0` and held `99.45--99.69 Hz`, SPU
    `44.0--44.1 kHz`, with zero audio underruns/overflows.
  Both processes were closed cleanly and no exact candidate process remains.
- This confirms the user’s “ran extremely well” result is reproducible from the
  fresh race-start and post-race states on the rebuilt candidate. The moving
  interpreter tail is still bounded path coverage; do not claim all execution
  is native or true PAL8 until those separate lanes are closed.

## True PAL8 capture/compile pass (2026-08-24 12:25)

- The true PAL8 candidate is
  `build-remaster-pal8-400-20260824/Wipeout_3_Special_Edition__Recompiled_pgxp.exe`
  (SHA256 `AD244157AECCB117A9EEF557B511A96EAA0ED7E4F99CAD309C9198176264DD85`).
  Its sibling config is the 8 MiB `pal8` image, PALx2, 400% CPU clock, and
  PGXP/HD disabled for the performance A/B.
- The PAL8 attract/menu capture (`overlay_captures.json`, seven regions) passed
  `compile_overlays.py --check` and the real compile with the current B24
  recompiler: `built OK: 7`, `failed: 0`, `unsupported: 0`, `unknown: 0`.
  All seven DLL/range pairs are published under the exact active cache tag
  `cg11_b24a140a_gc3d97ace3_f2`; no stale shards were copied.
- Relaunching the PAL8 attract state with the sibling config, absolute BIOS and
  memcard paths, `PSX_REWIND=0`, and HLE scheduling restored `pc=0x80101E64`.
  The run reached 1,845 frames with 38 registered native functions, 240,120
  native dispatches versus 44,531 interpreted dispatches, zero invalidations,
  zero unsupported midblocks, and clean shutdown. This is a valid PAL8
  performance result for the attract/menu state, not a race/replay result.
- The PAL8 24-bit FMV stripe remains unpatched: paired presentation evidence
  shows the defect before final OpenGL composition, and no safe source fix has
  been established. A PAL8-specific race-start and replay state is still needed
  because the owner’s plain-PAL states cannot validate PAL8 media/code paths.

## Exact process-guard validation (2026-08-24 12:55)

- `tools/run_candidate_single.ps1` was exercised against the true PAL8
  executable with `-AllowConfigMismatch`, an absolute BIOS, and the PAL8 state
  directory. It returned `candidate_start_ok` after the bounded startup check.
- The candidate was then closed/killed by its exact executable path; a follow-up
  CIM query confirmed zero remaining processes for that path. The guard’s
  cleanup scope did not touch any other WipEout/PSXRecomp process.

## Fresh exact-tail publication (2026-08-24 12:35)

- The current plain fresh-state capture was re-run through
  `compile_overlays.py` with the B24 recompiler, CPS flavor 2, and no force or
  stale-shard aliasing. The transaction published 80 exact-demand shards and
  skipped 3 cached/data-only entries with `PSX_SHARD_RESULT ok=80 failed=0`.
- The candidate then audited `indexed=152` under the active
  `cg11_b24a140a_gc3d97ace3_f2` namespace. Slot 1 restored at
  `pc=0x8015C404`; slot 2 restored at `pc=0x801722B0`; both exited cleanly
  with no competing WipEout/PSXRecomp/compiler process left behind.
- The post-publication run report recorded 153 registered overlay functions,
  1,850,184 native dispatches versus 140,438 interpreted dispatches, 45 loads,
  zero invalidations/revalidations, and zero unsupported interpreter midblocks.
  The remaining interpreter work is bounded path coverage, not a cache-hash or
  invalidation storm. A claim of zero interpretation would still require
  further exact captures; broad observed-PC admission is unsafe.

## Diagnostic tail reduction (2026-08-24 12:45)

- A slot-2 run with `PSX_RUNTIME_PERF_DIAG=1` held `guest=99.27--99.77 Hz`,
  `spu=44000--44127 Hz`, and zero underruns/overflows. Native dispatch was
  about `213k--229k/s`; interpreted dispatch was `13k--19k/s`, with loader
  invalidations and revalidations both zero.
- After publishing the newly flushed `0x80780000` capture as
  `00780000_C46BF7EE.dll/.ranges`, a second run held `guest=99.34--99.77 Hz`
  with native `223k--241k/s` and interpreted `6k--8k/s`. The hot interpreter
  moved to another exact tail (`0x000F5BA4`), confirming bounded coverage churn
  rather than a global timing defect. Do not broaden admission to observed PCs.

## Process guard hardening (2026-08-24 13:05)

- `tools/run_candidate_single.ps1` previously filtered CIM processes by the
  literal PGXP filename before applying its exact-path check. It now enumerates
  processes and matches only the resolved executable path, covering both the
  PGXP sibling and a future clean base `...Recompiled.exe` without widening
  cleanup to other builds.
- The script parses successfully, and the existing interpreter and static
  overlay guard tests pass after the change. A true PAL8 launch was admitted by
  the guard and then exact-path cleaned; no candidate process remained.

## Max-mod replay and canonical PAL8 base (2026-08-24)

- The user’s slot 1/slot 2 pair remains the reproducible plain-PAL 8 MiB
  400% A/B pair. With `PSX_REWIND=0`, slot 1 restored `0x8015C404` and slot 2
  restored `0x801722B0`; the replay sample held `99.44--100.03 Hz`, SPU
  `43.8--44.3 kHz`, zero audio errors, zero loader invalidations, and roughly
  `119--123k` native versus `3--4k` interpreted dispatches/s. This is the
  clean proof that the earlier replay invalidation storm is not present in the
  compiled candidate. It is not true-PAL8 media evidence.
- The canonical non-PGXP true-PAL8 executable is
  `build-remaster-pal8-off-20260824/Wipeout_3_Special_Edition__Recompiled.exe`.
  Its staged config is true 8 MiB PAL8/PALx2/400% with rewind compiled out.
  The PAL8 attract capture was compiled through the normal B24/f0 pipeline
  (`PSX_SHARD_RESULT ok=3 failed=0 skipped=3`), and the cache audits as
  `cg11_b24a140a_gc3d97ace3_f0`. A post-build boot-state check held
  `99.49--100.27 Hz`, SPU about `44.1 kHz`, zero audio errors, and native
  dispatch was active (`~57k/s`).
- The narrow static-overlay invalidation fix is committed in the psxrecomp
  submodule as `c74246ea`: only the exact high-RAM image
  `[0x00780000, 0x00782004)` is armed for generation tracking at boot, with a
  focused `perf_round3_guards_test`. Direct Python and ctest checks pass; the
  rebuilt canonical PAL8 base links and launches. This is invalidation
  bookkeeping only and does not widen overlay admission.
- Open work: capture fresh true-PAL8 race-start/replay states (the current
  user pair is plain-PAL), verify in-race/menu UI and FMV transition on PAL8,
  and publish only exact PAL8 path overlays. Keep `PSX_REWIND=0` for all
  performance comparisons. Do not run two exact candidates concurrently.

## PAL8 follow-up guard and live readiness (2026-08-24)

- The canonical non-PGXP PAL8 base was relaunched with `--no-launcher`, an
  absolute SCPH-1001 BIOS, the isolated `saves-pal8-20260824` directory, and
  `PSX_REWIND=0`. It restored the PAL8 attract state at
  `pc=0x80101E64` and remained responsive at roughly `99.5 Hz` / `44.1 kHz`
  with zero audio underruns or overflows.
- No PAL8 slot 1 or slot 2 state exists yet. Do not use the plain-PAL
  `saves/scph1001` pair as PAL8 evidence; fresh PAL8 race-start and replay
  states remain the required interactive coverage.
- A forward declaration added for the exact static-overlay watch made
  `test_dirty_text_continuation_guards.py` slice the declaration instead of
  `dirty_ram_reset_for_boot()`. The guard now anchors its terminator after the
  reset implementation (`psxrecomp` commit `3e81ca00`). All five structural
  guards and eight focused runtime tests pass.
