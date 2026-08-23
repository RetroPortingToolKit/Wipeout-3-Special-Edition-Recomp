# WipEout 3 PAL x2 / 100 Hz pacing audit

Status: **conditionally correct in the runtime, not release-ready as a default-on feature**.

Audit basis:

- Product tree `892e3501ea318afaaf3c7576b651f9af593ad23b`.
- Runtime submodule `psxrecomp` at `1b860bae0497a1d5ac8b0259d8a9a0f23b073c0e`.
- Existing measurements recorded in `docs/HANDOFF.md`.
- Pure runtime tests `runtime/tests/test_guest_clock.c` and `runtime/tests/test_frame_pacing.c`, compiled and run from the pinned runtime; both passed.

No runtime, GPU, PGXP, build, generated-overlay, game, or mod file was changed for this audit.

## Executive verdict

| Area | Verdict | What is actually true |
| --- | --- | --- |
| PAL x2 guest cadence | Pass by construction | PAL's 677,376-cycle CRTC period is divided by two to 338,688 cycles. At the 33.8688 MHz guest clock that is 100 VBlank edges/s. |
| Gameplay simulation cadence | Conditional | x2 doubles guest VBlank/IRQ opportunities. It doubles WipEout simulation only when the authenticated title build selects `gSimVblankSkip=1`; the runtime hook alone cannot do that. |
| FMV cadence | Correct in the current branch | The PAL-x2 path keeps the guest/audio clock at 100 Hz but presents one of every two active depth-24/MDEC ticks, preserving the authored PAL 50 Hz presentation. The skipped tick still gets paced. |
| Audio sample clock | Pass by arithmetic; output health conditional | SPU production is guest-cycle based at 768 cycles/sample, hence 44,100 frames/s at real-time speed. Host bridge underruns can still occur when the emulator cannot sustain the 10 ms work budget. |
| Host pacer | Correct policy, insufficient throughput under load | It has a monotonic deadline and bounded debt recovery, but it cannot make a frame that takes more than 10 ms run at 100 Hz. Sustained over-budget work settles below target. |
| VSync interaction | Correct XOR in effective code | Active native PAL x2 forces swap interval 0 and leaves wall pacing authoritative. This avoids double-blocking, but an activation comment still describes a matching-panel VSync branch that the effective policy does not take. |
| “100 Hz” telemetry | Easy to misread | `VBlank` is guest edge cadence; `Render` is a display-area flip counter; neither alone is a backend swap counter or a game simulation-update counter. |

## Clock relationships

The pinned runtime has one guest CPU clock and derives the relevant clocks from it:

```text
guest CPU                         33,868,800 cycles/s
PAL stock CRTC                   677,376 cycles/VBlank = 50 VBlank/s
PAL x2 CRTC                      338,688 cycles/VBlank = 100 VBlank/s
SPU                              768 guest cycles/sample = 44,100 samples/s
PAL x2 audio per VBlank          338,688 / 768 = 441 samples
```

This is implemented in `psxrecomp/runtime/include/guest_clock.h:14-44` and exercised by `psxrecomp/runtime/tests/test_guest_clock.c`. The test also verifies that splitting servicing into arbitrary event-sized chunks preserves the fractional cycle carry. Therefore, multiplying the CRTC rate must **not** multiply SPU output to 88.2 kHz.

The CRTC path is also direct: `psxrecomp/runtime/src/gpu.c:3281-3312` stores the multiplier and returns `base / multiplier`; `psxrecomp/runtime/src/interrupts.c:397-429` subtracts that period and fires as many VBlank edges as guest cycles require. `gpu_vblank_tick()` calls the mod hook and the presentation callback at each edge (`psxrecomp/runtime/src/gpu.c:3016-3083`). There is no CPU-clock or per-instruction speed multiplier in this path.

## Does x2 double simulation or only render?

It does both only when the title-side gate is configured for it, and those are separate facts:

1. The runtime x2 hook requests native 100 Hz plus CRTC multiplier 2 (`src/mods/wipeout3_framerate_100_plugin.c:12-14`). This doubles guest VBlank/IRQ callbacks while keeping guest cycles per wall second unchanged.
2. The title verifier defines selector 0 as `gSimVblankSkip=2`, selector 1 as skip 1 with PAL-x2 state 0, and selector 2 as skip 1 with PAL-x2 state 1 (`tools/verify_pal100_title.py:30-45,61-64`). Selector 2 also stores the skip value at `0x8017F02C` (`tools/verify_pal100_title.py:31-40`).
3. Consequently, an unpatched executable with `gSimVblankSkip=2` can receive 100 VBlank edges while advancing its gated simulation on every second edge: about 50 simulation updates/s. That is a display/interrupt-rate change, not proof of 100 Hz gameplay.
4. The selector-2 title transform is the necessary gameplay contract. The verifier proves the expected words, strings, and selector semantics, but it does not prove that every game-side time step, physics constant, animation table, or replay assumption was retuned. That remains a title-build validation obligation.

The current checkout has no `disc/` payload, so `python tools/verify_pal100_title.py --json` correctly reports missing Track 01/executable and cannot certify the live selector-2 image here. That is an evidence limitation, not a runtime pass; release must run the verifier against the authenticated user media before enabling the package.

The mod source itself states this boundary: the hook cannot make an unmodified PAL executable simulate twice per PAL VBlank (`src/mods/wipeout3_framerate_100_plugin.c:3-10`). This is the correct architecture. The concrete deployment problem is that the current PAL100 manifest is `default_enabled = true` (`mods/preloaded/packages/wipeout3.enhancement.framerate-pal100/1.0.0/manifest.toml:16-21`) even though the runtime contract says the title transformation and hook must be selected together. Until the authenticated selector-2 build is required and verified at launch, default-on can silently produce “100 Hz” VBlank telemetry with 50 Hz gameplay.

## Gameplay pacing and the host pacer

When PAL x2 is active, `refresh_live_crtc_cadence()` arms the multiplier only after the PAL game display mode is programmed (`psxrecomp/runtime/src/main.cpp:2978-3027`). This avoids accelerating BIOS/boot. It then sets a 10.0 ms native wall period for the requested 100 FPS.

`frame_pacer_wait()` is a deadline pacer (`psxrecomp/runtime/src/frame_pacing.c:14-76`):

- It samples once per sleep decision, compares before subtracting, sleeps at most one period, and spins for the final sub-millisecond interval.
- If a frame is late but within the 12-period catch-up window, it advances one deadline without sleeping. This repays transient debt but produces a burst of short inter-frame intervals.
- If it is more than 12 periods late, it re-anchors and forgives the debt. This prevents an unbounded catch-up storm but permanently loses guest time relative to wall time.

The measured failure mechanism is therefore straightforward:

- At 100 Hz, the normal work budget is 10 ms.
- If a race frame repeatedly takes 13 ms, the pacer is late every frame and the actual cadence converges near 76.9 Hz; no pacing code can recover the missing 3 ms.
- If a single FMV/CD/GPU stall exceeds roughly 120 ms, the re-anchor drops the accumulated guest/audio debt. The next frames run at the new wall anchor rather than catching up the missing guest time.
- Existing measurements in `docs/HANDOFF.md` match this mechanism: an early 100 Hz run delivered only about 52.6 guest frames/s; later Release runs reached roughly 74–79 Hz with audio underruns. Headless/turbo runs near 200 Hz are intentionally uncapped and are not evidence of real-time 100 Hz support.

The fix is performance/route-specific, not a clock multiplier: keep the full race path under the 10 ms budget (the handoff identifies the hot interpreter fallback and render/PGXP/GPU work as the current cost centers), then verify that no pacer debt accumulates. Changing the audio clock or enabling VSync cannot fix an over-budget guest quantum.

## FMV cadence

The current PAL-x2 movie branch is conceptually correct. In `psxrecomp/runtime/src/main.cpp:6976-7020`, when MDEC is active and the display is depth-24:

- the guest still advances at 100 VBlank ticks/s;
- the presentation divisor is 2, so the authored PAL movie is presented at about 50 swaps/s;
- the skipped presentation calls `frame_pacer_wait()` before returning (`:7007-7014`), so skipping a movie image does not accidentally uncap the guest.

That last condition is a concrete regression guard. Before it was present, the alternate-present return path could report about 200 Hz: it skipped both the image and the wait. A future FMV optimization must preserve “presentation-only skip” semantics: skip the swap, never skip guest-cycle advancement, CD timing, SPU pumping, or the 10 ms wait.

The separate `fmv_skip_active`, headless, debug-turbo, and load-turbo returns intentionally bypass wall pacing (`psxrecomp/runtime/src/main.cpp:6840-6861` and `:6967-6974`). Those modes are useful for fast validation, but their frame rate must not be used as a PAL100 real-time result.

The expected diagnostic signature is therefore:

```text
race: VBlank ~= 100/s, gameplay presents ~= 100/s
PAL FMV: VBlank ~= 100/s, actual GL/backend swaps ~= 50/s
both: guest audio production ~= 44,100 frames/s, no pitch doubling
```

## Audio sample clock and failure modes

`sdl_audio_pump()` takes `psx_cycle_count` deltas and converts them with the 768-cycle carry-preserving formula (`psxrecomp/runtime/src/main.cpp:3191-3248`). It does not count host presents. The host callback pulls from the bridge (`:2136-2143`), so the following are different failures:

1. **Guest under-speed:** guest cycles/wall-second fall below 33.8688 MHz because the 100 Hz workload is too slow. The SPU correctly produces fewer than 44,100 frames/s and the host bridge eventually underruns.
2. **Bridge underflow despite correct guest production:** guest audio counters are near 44,100/s, but the host callback consumes an empty bridge during a scheduling gap. This is an output/pacer problem, not an SPU-rate problem.
3. **Intentional debt drop after a long stall:** offline audio caps a single pump above 2,048 frames and drops the remainder (`:3221-3229`). At 44.1 kHz, 2,048 frames is about 46.4 ms. A longer blocked quantum can therefore lose audio time without implying 88.2 kHz or a simulation-rate error.

The existing telemetry distinguishes these: `audio_stats` reports SPU/CD/host tap frame counts, pump skips, trace underruns, bridge fill/target, output underruns, overflow drops, and correction (`psxrecomp/runtime/src/debug_server.c:6669-6733`). The internal `psx_runtime_timing_snapshot()` exposes guest cycles, guest VBlanks, CRTC period/multiplier, host period, native-rate state, and audio guest-cycle/frame counters (`psxrecomp/runtime/src/main.cpp:2106-2134`).

The fix for an output underrun is to remove the sustained guest/pacer deficit or adjust the bridge scheduling target after measuring it. Do **not** multiply SPU production for PAL x2; that would make the sample clock wrong and pitch CD/XA/audio effects upward.

## VSync and presentation ownership

The effective policy is an XOR:

- `present_vsync_owns_cadence()` returns false for active native VBlank (`psxrecomp/runtime/src/main.cpp:2866-2885`).
- `present_effective_swap_interval()` consequently returns 0 (`:2888-2904`).
- The offline wall pacer runs before present when VSync does not own cadence (`:7022-7034`).
- If a blocking swap owns cadence in another mode, `psx_present_swap_paces()` tells frame-skip logic not to skip an unpaced image (`:2914-2922`).

This is the correct protection against two independent 100 Hz waits. It also means the *effective code* keeps the wall pacer authoritative even on a panel that happens to be exactly 100 Hz. The activation comment at `:1380-1402` still describes allowing matching-panel VSync to own cadence; that comment is stale relative to the explicit native-rate branch at `:2875-2881`. The fix is to make the comment and test matrix agree with the chosen policy. Re-enabling matching-panel VSync without measuring `SwapWindow` would reintroduce the old double-blocking/host-jitter failure.

The renderer also has a slow-present self-heal for SDL presents (`psxrecomp/runtime/src/main.cpp:7589-7612`), but that is not a substitute for measuring the GL swap stream. A driver/compositor can still block a nominally interval-0 present.

## What the counters mean

Do not use one “FPS” number as proof:

- `vblank_rate`: cycle-paced raise and delivered counts; this is the guest VBlank authority (`psxrecomp/runtime/src/debug_server.c:5497-5529`).
- `VBlank`/speed telemetry: counts simulated VBlank callbacks, not swaps (`psxrecomp/runtime/src/main.cpp:6424-6450`).
- `Render`: `psx_gpu_display_flip_count()`, a display-area flip counter, not necessarily a backend swap counter.
- `present_ring`: one entry per present decision and its content classification (`psxrecomp/runtime/include/present_ring.h:10-42`).
- `gl_present_ring`: every GL `SwapWindow`, with frame number and wall-clock timestamp (`psxrecomp/runtime/src/debug_server.c:9371-9408`). This is the counter to use for the FMV 50 Hz presentation claim.
- `audio_stats`: guest-side taps versus host-consumed frames and output health.
- `frame_perf`, `latency`, and `PSX_HITCH_DIAG=1`: identify whether the missing 10 ms is guest work, pacer time, or host/present time.

The current observability gap is a direct game-simulation-update counter. A 100 Hz VBlank count plus `gSimVblankSkip=1` in RAM is strong evidence, but a future runtime change should expose a title-gate pass counter so “100 Hz gameplay” is directly measurable rather than inferred from a package selector and VBlank count.

## GooseStation / DuckStation comparison

The relevant DuckStation concept is separation of clocks and responsibilities, not a particular 100 Hz switch:

- DuckStation’s core frame loop throttles on a target frame period and generates pending SPU samples before sleeping; its frame number/presentation decisions are tracked separately from the internal emulation frame.
- Its video settings distinguish VSync, host-refresh synchronization, and optimal frame pacing. Those are presentation/throttle policies, not a title-side simulation patch.
- Its SPU timing is driven by emulated system timing rather than by the number of host swaps.

Primary references: [DuckStation system loop](https://github.com/stenzek/duckstation/blob/master/src/core/system.cpp), [DuckStation SPU timing](https://github.com/stenzek/duckstation/blob/master/src/core/spu.cpp), and [DuckStation video pacing settings](https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/emulationsettingswidget.cpp).

The local GooseStation concept is explicit in the pinned headers: `psxrecomp/runtime/include/gpu.h:80-83` calls the multiplier “GooseStation-style Nx video timing,” and `psxrecomp/runtime/include/mod_plugins.h:101-114` separates the CRTC period divisor from the explicit wall-clock native-rate override. That is the same useful separation found in the DuckStation design: CRTC/VBlank is guest timing, `frame_pacer_wait()` is real-time throttling, present rings are presentation evidence, and SPU output is guest-cycle based. The local “GooseStation-compatible” wording in the PAL100 manifest should therefore be read as a target/package compatibility claim, not as evidence that a host refresh or display multiplier changes WipEout’s internal simulation. The title selector and its authenticated media remain authoritative for that part.

## Release gate and exact test

PAL100 should remain opt-in until all of the following are true on the authenticated selector-2 title build, with netplay, rewind, turbo, debug turbo, interpolation, and auto-skip disabled:

1. `vblank_rate` shows cycle-paced raises and delivered edges at 99.5–100.5/s, with no poll-fallback raises.
2. A full race sustains the same rate; `PSX_HITCH_DIAG=1` shows no sustained >10 ms guest/present quantum and `frame_perf` identifies enough headroom below 10 ms.
3. Gameplay `gl_present_ring` is near 100/s; FMV `gl_present_ring` is near 50/s while `vblank_rate` remains near 100/s.
4. `audio_stats` and the timing snapshot show guest production near 44,100 frames/s, host consumption at the opened device rate, and no increasing output-underrun/overflow counters.
5. Live title RAM and/or the authenticated verifier prove selector 2, `gSimVblankSkip=1`, and PAL-x2 state 1. A VBlank count alone is insufficient.
6. Repeat on 60, 100, and 120 Hz host panels. The measured owner must remain exactly one of wall pacer or blocking VSync; never both.

If (1) fails, investigate guest/host throughput. If (1) passes but gameplay updates or race presentation do not, investigate the title gate or presentation path. If guest audio is 44.1 kHz but host underruns increase, investigate bridge scheduling. If FMV swaps are 100/s, the presentation divisor has regressed. These signatures identify the fix without changing the guest audio clock.
