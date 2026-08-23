# Four-feature performance audit: 900% CPU, PAL x2/100 Hz, PGXP, HD, and 8 MB

Date: 2026-08-22  
Target: WipEout 3 Special Edition recompiled build  
Scope: explain the severe low-FPS/half-speed symptom when the current high-detail
profile combines 900% CPU overclock, PAL x2/100 Hz, PGXP, HD replacement, and
8 MB RAM. This is an evidence audit only; no runtime, shared configuration,
renderer, PGXP, or mod file was changed.

## Executive result

The four features are not four independent frame-rate multipliers. PAL x2
raises the guest VBlank opportunity and imposes a 10 ms real-time quantum;
900% reduces CPU retirement charges but does not multiply the peripheral clock;
PGXP and HD replacement add work to the geometry/render and texture paths; 8 MB
mostly changes the address space and enables the relocated enhancement engine.

The top bottleneck is therefore **per-100-Hz-tick CPU-side guest/render work,
with the high-detail relocated path’s interpreter/dispatch fallback as the
first suspect**. The evidence does not justify attributing the failure to 8 MB
allocation or to PAL audio timing. At 100 Hz every tick has 10 ms. A sustained
13 ms tick converges to about 77 Hz, and a 20 ms effective cadence presents as
half-speed. The pacer cannot recover work that exceeds its deadline; it either
runs late or eventually drops debt.

## Evidence

| Question | Bounded evidence | Interpretation |
|---|---|---|
| Is PAL x2 itself the clock error? | `docs/audits/pacing.md` derives 338,688 cycles/VBlank and 100 VBlank/s; SPU remains 44,100 frames/s. Guest-clock tests passed PAL stock/x2, SPU, and CDDA checks. | No. x2 changes VBlank opportunity, not a 9x CPU/audio clock. |
| Does the real-time path sustain the target? | `docs/HANDOFF.md` records an early windowed result of ~52.6 guest/s, later Release ~74–79 guest/s with audio underruns, and a netplay/rewind-off control at 99.7–100.2 Hz with zero underruns. | The failure is workload/scheduling pressure, not a fixed PAL-x2 arithmetic defect. |
| Does 900% remove the pressure? | `docs/audits/hardware-parity.md` states that 900% scales retired CPU instruction charges only; CRTC, SPU, and CD remain in the normal cycle domain. It also records a high-PC instruction guard/interpreter fallback. | The overclock helps only native guest retirement; a fallback can dominate and erase the expected gain. |
| Is 8 MB the frame-rate cause? | The same audit records unique 8 MB activation before `memory_init()` and actual execution at `0x80780AF8`; the high-PC guard is classified as a performance fallback, not a RAM fallback. | 8 MB is an enabler/route selector. The meaningful cost is code execution mode, not the allocation itself. |
| Could PGXP/HD be material? | `docs/audits/renderer-quality.md` requires separate emulation, upload/lookup, and presentation measurements; `docs/audits/hd-assets.md` records 47,919 replacement hits, 300 decoded images, and zero decode failures. | Both add plausible per-frame work, but current evidence does not rank them above the observed CPU fallback. HD discovery/decode success is not a throughput proof. |
| Is the “FPS” counter proving swaps? | `docs/audits/pacing.md` distinguishes VBlank, Render/display flips, `present_ring`, and `gl_present_ring`. | Use backend swap timestamps and frame timing, not VBlank or display-area flips alone. |

## Interaction model

The likely chain is:

```text
PAL x2 -> twice the VBlank opportunities -> 10 ms tick budget
900%   -> cheaper native CPU retirement, but no relief for fallback dispatch
PGXP   -> extra geometry/depth/transform work on 3D frames
HD     -> replacement lookup/upload/cache work on textured frames
8 MB   -> relocated high-detail code becomes reachable; guard/interpreter path matters
```

When the 8 MB high-detail path hits the recorded high-PC instruction guard,
the 900% native retirement advantage is no longer a sufficient explanation for
throughput. PGXP and replacement work then consume the remaining budget. The
observed 74–79 Hz Release result is consistent with a tick that is modestly
over 10 ms; the ~50 Hz/half-speed symptom is consistent with a larger stall or
an effective second 20 ms wait. This is a causal hypothesis supported by the
measurements, not a claim that the current evidence has apportioned every
microsecond among PGXP, HD, and dispatch.

## Narrow proposed fix and test

Do one four-way, fixed-route A/B capture before changing code:

1. Keep the authenticated PAL-x2 title and 8 MB profile fixed. Disable only
   PGXP and HD replacement, then re-enable each independently, followed by
   both together. Keep netplay, rewind, turbo, auto-skip, and debug bypasses
   off.
2. Capture a 30-second on-track window in Release with `frame_perf`,
   `PSX_HITCH_DIAG=1`, `vblank_rate`, `gl_present_ring`, and `audio_stats`.
   Record median/p95 tick time, guest/present cadence, replacement hits, and
   audio underruns. Do not use headless or uncapped results.
3. In the same route, compare the high-detail relocated region with native
   dispatch versus the recorded guarded/interpreter path. If the guard is
   hit, the narrow implementation candidate is to add the missing generated
   entry/translation coverage for that specific high-PC instruction path—not
   to alter PAL timing, SPU rate, or the 900% scale.

Acceptance for the proposed fix is at least 99.5–100.5 guest/present Hz on the
race window, no increasing audio underruns, and no sustained tick p95 above
10 ms. If the native-dispatch A/B passes while PGXP+HD remains enabled, the
guard/fallback is the top bottleneck. If it does not, use the isolated PGXP/HD
cost deltas to select the next narrow renderer or replacement-cache change.

## Boundary

This audit intentionally does not edit runtime core/shared config, generated
code, PGXP/GPU implementation, or active mods. It records the strongest
available evidence and a bounded experiment that can distinguish the leading
cause without turning a diagnostic run into a release claim.
