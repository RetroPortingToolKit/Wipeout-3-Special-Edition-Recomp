# GooseStation PAL x2 / 100 Hz source-flow audit

This audit follows the PAL x2 path from the title selector through the
recompiled PSX timing devices, host pacing, FMV/audio, and the failure modes
that can look like a successful 100 Hz result. It compares the local WipEout
integration with the GooseStation-style skip/pacing boundary documented in
`docs/audits/goosestation-skip-engine.md` and `docs/audits/pacing.md`.

## Executive result

The design has the right separation of concerns, but PAL-100 is not proven by
one FPS number and must remain experimental/default-off until the trace gate at
the end passes. The required chain is:

```text
authenticated PAL title selector
  -> gSimVblankSkip=1 and PAL-x2 title state
  -> PAL CRTC period / 2 = 100 guest VBlank edges/s
  -> simulation gate executes on every edge
  -> 10 ms host deadline pacing
  -> 44.1 kHz guest audio and protected 50 Hz PAL FMV presentation
```

Breaking any link produces a plausible but false result: 100 guest VBlanks
with 50 Hz simulation, an uncapped 200 Hz diagnostic, 50 Hz FMV that is
mistaken for a game stall, or nominal 44.1 kHz production with host audio
underruns.

## Source evidence

### Video standard, title selector, and simulation gate

The runtime activation hook is deliberately only a timing hook:
`src/mods/wipeout3_framerate_100_plugin.c:3-14` says that it cannot make an
unmodified PAL executable simulate twice per VBlank, then requests native 100
Hz and CRTC multiplier 2. This is the correct GooseStation boundary: title
code owns simulation semantics; the emulator owns device and wall-clock
cadence.

The authenticated title verifier pins the PAL target to game ID SCES-02845 and
the exact Track 01/EXE SHA-256 values in `tools/verify_pal100_title.py:21-24`.
Its exact instruction surface is `:31-40` and its title strings are
`100HZ`/`50HZ` at `:43-46`. The verifier's selector model at `:61-64` is the
critical acceptance rule:

| selector | `gSimVblankSkip` | PAL-x2 title state |
| ---: | ---: | ---: |
| 0 | 2 | 0 |
| 1 | 1 | 0 |
| 2 | 1 | 1 |

Therefore selector 2 is necessary for 100 Hz gameplay. A PAL x2 CRTC paired
with selector 0 still delivers 100 VBlank edges but advances a skip-every-
other-edge simulation at about 50 updates/s. The title verifier is a static
media-authentication gate, not an end-to-end runtime proof. In this checkout,
the user disc payload is absent, so a local verifier failure for missing media
must not be interpreted as a timing failure.

### CRTC / VBlank flow

The source evidence recorded in `docs/audits/pacing.md:32-40` gives the clock
arithmetic:

```text
PAL stock: 677,376 guest cycles/VBlank = 50 edges/s
PAL x2:    338,688 guest cycles/VBlank = 100 edges/s
SPU:       768 guest cycles/sample = 44,100 samples/s
```

The same audit traces the implementation to the `psxrecomp` runtime:
`runtime/src/gpu.c:3281-3312` stores the refresh multiplier and returns the
base period divided by it; `runtime/src/interrupts.c:397-429` consumes guest
cycles and fires every due VBlank edge; `runtime/src/gpu.c:3016-3083` invokes
the VBlank hook and presentation callback. This is a peripheral cadence
change, not a CPU instruction-speed multiplier. The `psxrecomp` gitlink in
this worktree is not checked out, so those cited lines are retained upstream
source evidence rather than files independently re-read in this checkout.

The PAL multiplier must not be armed during BIOS boot. The retained source
trace in `docs/audits/pacing.md:55-57` records the guarded behavior: the live
CRTC cadence is refreshed only after the PAL game display mode is programmed.
Arming it at process start changes BIOS cadence and is a boot-path defect, not
evidence of a faster title.

### CPU pacing and host pacer

`docs/audits/pacing.md:59-72` identifies `runtime/src/frame_pacing.c:14-76` as
a monotonic deadline pacer with bounded debt recovery. At 100 Hz the work
budget is 10 ms. If a frame needs 13 ms, the achievable cadence converges near
76.9 Hz; no pacer can recover work that does not fit the deadline. A long stall
(about 120 ms in the retained analysis) re-anchors instead of replaying all
missing guest/audio time.

This explains the retained receipts in `docs/HANDOFF.md`: an early windowed
run delivered about 52.6 guest frames/s; optimized Release runs improved to
roughly 74-79 Hz but still had audio underruns. Headless values near 200 Hz
are intentionally uncapped diagnostics and cannot certify real-time PAL-100.
The acceptance metric must be a bounded, windowed, wall-clock run with the
host pacer enabled.

### Title frame-skip versus presentation skip

Title simulation skip and FMV presentation skip are different mechanisms.
`docs/audits/pacing.md:46-51` establishes that the title's selector state
controls whether simulation consumes each guest VBlank. The runtime cannot
repair a wrong title state.

For PAL FMV, the source trace at `docs/audits/pacing.md:74-84` records the
intended behavior in `psxrecomp/runtime/src/main.cpp:6976-7020`: guest cadence
remains 100 Hz, depth-24/MDEC presentation uses a divisor of 2, and the
alternate presentation path still calls `frame_pacer_wait()` before returning.
Skipping the wait as well as the image previously produced a false roughly
200 Hz result. The invariant is: presentation may be skipped; guest cycles,
CD timing, SPU pumping, and the 10 ms wait may not be skipped.

### FMV and audio

PAL authored movie presentation is about 50 Hz even while PAL x2 guest timing
is 100 Hz. The correct observables are thus approximately 100 guest VBlanks,
approximately 50 actual movie/backend swaps, continuous MDEC/CD/XA activity,
and no timing-induced audio loss. `docs/audits/pacing.md:32-38` requires SPU
production to remain 44,100 frames/s, not 88,200. The retained FMV evidence in
`docs/HANDOFF.md` reports stock PAL at about 20 ms cadence and PAL x2 guest
10 ms cadence with movie swaps about every 20 ms; this is a presentation pass,
not a full media soak.

Audio health has two layers: guest production arithmetic and host bridge
health. A run can produce the expected 44.1 kHz guest stream yet underrun at
the host when 10 ms work exceeds available CPU time. The GooseStation audit
therefore treats new SPU trace underruns, host output underruns, bridge
overflows, and missing XA/CDDA delivery as failures, not as harmless telemetry.

## GooseStation comparison and half-speed failure modes

The existing GooseStation skip-engine audit (`docs/audits/goosestation-skip-engine.md:200-220`)
requires phase-specific predicates, explicit start/stop edges, media vetoes,
and endpoint plus continuation comparison. That model applies here even
though PAL x2 is cadence acceleration rather than frame skipping:

1. BIOS shell skip, game FMV skip, host pacing, and emulated CD timing are
   separate axes (`docs/audits/boot-cd.md:65-75,109-110`). Never use a fast-boot
   or FMV-skip receipt as PAL simulation evidence.
2. Any MDEC, XA, CDDA, or interactive-input overlap invalidates a candidate
   GooseStation load-skip interval (`docs/audits/goosestation-skip-engine.md:254-272`).
   For PAL-100, those signals instead must remain continuous and ordered.
3. A 100 Hz VBlank counter with selector 0 is the principal half-speed trap:
   the display/IRQ rate doubled while gameplay remains about 50 Hz.
4. A 50 Hz movie swap counter is expected during FMV and is not a half-speed
   gameplay result.
5. Headless/turbo or skipped-wait results near 200 Hz are uncapped artifacts.
6. Nominal 44.1 kHz SPU production with host underruns is an overloaded host,
   not a successful real-time run.
7. Arming CRTC x2 before PAL mode changes BIOS timing and can create a false
   boot or media-load diagnosis.
8. VSync plus the 10 ms pacer can double-block; the effective PAL-x2 policy
   must keep swap interval 0 and leave the wall pacer authoritative
   (`docs/audits/pacing.md:22-23`).

## Trace acceptance test

Use the authenticated PAL Track 01/EXE and retail BIOS, PAL x2 enabled, PAL
100 title selector explicitly selected, FMV skip/headless/debug turbo disabled,
and identical input/card state for at least three normal runs. Capture one
stock PAL control run and one PAL-x2 run from the same checkpoint, then compare
the following fixed wall-clock window (minimum 30 seconds in title/menu and a
minimum 30 seconds on track; split before/after FMV rather than hiding it):

| Trace | PAL x2 acceptance |
| --- | --- |
| Video standard | Programmed PAL CRTC observed before x2 arm; no BIOS x2 cadence |
| Guest timing | 100 ±1 guest VBlank/s over the settled on-track window; no long pacer debt/re-anchor |
| Simulation | Selector 2 receipt; 100 simulation-update/gate executions/s, independently of VBlank counter |
| CPU pacing | Pacer enabled; per-frame work and deadline-debt distribution retained; no uncapped path |
| FMV | 100 guest ticks/s, about 50 movie/backend presents/s, continuous MDEC/XA/CDDA; no skipped wait |
| Audio | Guest SPU/CDDA 44,100 ±0.5% frames/s; zero new SPU trace underruns, host underruns, or overflows |
| CD/media | Exact command, sector, IRQ/DMA, XA/CDDA order against the paired route; no missing or duplicated events |
| State/gameplay | Same title/menu/race endpoint and identical RAM/MMIO/frame fingerprint through the compared route, allowing only documented x2 cadence fields |

The test fails immediately if selector 2 is not observed, if guest VBlank and
simulation counts disagree, if the host pacer is bypassed, if FMV skips its
wait, if audio production is doubled or underruns, or if CD/XA/CDDA ordering
diverges. A passing static title verifier, a 100 Hz VBlank counter, a 50 Hz FMV
counter, or a headless 200 Hz run is individually insufficient.

## Current disposition

The source architecture and arithmetic are coherent, and the retained probes
show meaningful progress: title selector bytes are authenticated, PAL x2 FMV
presentation is separated from guest cadence, and standalone title/race probes
have reached approximately 100 Hz with 44.1 kHz guest production under
reduced workload. The full normal route still has open proof obligations for
long-run race stability, host audio health, and paired CD/card/state traces.
Accordingly this audit recommends keeping PAL-100 experimental/default-off
until the trace acceptance test passes on the full workload.
