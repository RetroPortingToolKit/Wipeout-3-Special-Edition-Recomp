# Native performance audit: PAL x2 continuation at `0x007814D0`

Status: audit only. No runtime, generated C, compiler, build, game, or mod files were changed.

## Conclusion

The remaining performance hypothesis is credible, but the exact native continuation is not yet proven. The physical address `0x007814D0` canonicalizes to the high-RAM address `0x807814D0`. Existing capture data shows that this address is real and hot; the current generated static dispatch does not actually own it.

Evidence from the canonical ignored artifacts:

- `build-release/overlay_captures-8078.json` contains 644 high-RAM captures, all loading at `0x80780000` with `0x2004` bytes. `0x807814D0` was executed in 103 captures and promoted as a dispatch seed in 96; seven captures observed it without promoting it. There are 632 distinct byte images, so the address is variant-sensitive.
- The target word is stable across the 103 target captures: `0x27BDFFC0` at `0x807814D0`, followed by a normal stack-frame prologue. This is a reproducible code entry, not an address-only artifact.
- `overlay-8078-gen.log` reports the target as `DISPATCH_ENTRY`, but `generated/overlays_static.c` has no `case 0x807814D0u`, `func_807814D0`, or `block_807814D0`. Its 84 target references are CPS transfers *to* the address, not an owning native dispatch case.
- The combined capture input has 1,701 records across many RAM regions. The generated static C is 192,151,662 bytes and reaches range index `41967`; this is a broad corpus, not a bounded proof of this one continuation.
- `build-release/psx_last_run_report.json` records a neighboring high-RAM interpreter guard at `0x80781474` (`0x1420FFDC`) and a nearby high-RAM store at `0x80780AF8`. It does not record a fallback at `0x807814D0`; `midblock_count` is zero. The same report has `overlay_loader.active=0`, `disp_native=0`, and `disp_interp=0`, so it cannot supply a fallback percentage.

The practical diagnosis is: the capture/generator discovery phase knows about `0x807814D0`, but the final static artifact does not provide the exact function or CPS resume route. With static dispatch first and the dynamic loader inactive, that address falls through to the dirty-RAM interpreter. The current artifacts establish a strong lead, not a measured percentage or a safety result.

## Relevant implementation behavior

`psxrecomp/tools/compile_overlays.py` already has the right bounded concepts:

- CPS compilation runs a fixed point of at most four root-discovery passes.
- `parse_cps_continuation_owners()` maps emitted `block_*` labels to their owning native host.
- `add_cps_resume_case()` only adds a resume arm when the owner really contains the requested block label; otherwise it fails closed.
- `generate_interior_fragment_static()` compiles one exact captured range and reports `requested-entry-not-emitted` when the requested root is absent.
- Static dispatch validates the live range/CRC before calling the generated symbol.

The runtime path in `psxrecomp/runtime/src/dirty_ram_interp.c` tries `psx_overlay_dispatch()` first, then `overlay_loader_dispatch()`, then interpretation. The dynamic CPS path can re-enter an already-loaded owner range with `cpu->pc=addr`, but it is disabled when the overlay loader is inactive. Its safety gates include exact CRC/delay-slot validation, device-touch rejection, native blocking/rank gates, and a 32-consecutive-pass differential budget (`OVERLAY_DIFF_BUDGET=32`). A bad entry is fail-closed to interpretation.

That dynamic machinery is not proof for a new static case: static dispatch returns before the dynamic diff harness, and the current report shows that dynamic loader path was inactive.

## Bounded next implementation pass

1. **Freeze one exact live input.** Select one target-bearing row from `overlay_captures-8078.json`, decode its 8,196 bytes, and record the image CRC, load address, size, target word, and target neighborhood. Do not recapture, merge captures, run the full static generator, or regenerate the 1,701-record corpus.

2. **Compile one exact target fragment with CPS enabled.** Invoke the existing isolated-fragment path with seed `0x807814D0`, the selected captured bytes, `--overlay`, and `PSX_CPS=1`. Keep the temporary output outside `generated/`. If the root is not emitted, use only the compiler-reported CPS owner/block relationship to choose the owner fragment; do not broaden the seed set.

3. **Require a structural dispatch proof before linking.** The fragment must show all of the following for the selected image:

   - an exact range/CRC record covering `0x807814D0`;
   - a static dispatcher case keyed by canonical `0x807814D0`;
   - either a native host containing `block_807814D0` plus a legal entry-switch arm, or a wrapper that sets `cpu->pc=0x807814D0` and enters the verified owner;
   - no `requested-entry-not-emitted`, missing-owner, or `psx_native_bad_entry` path for this target.

   A generated wrapper is acceptable; a broad corpus link is not. If the exact fragment cannot satisfy these conditions, stop and retain interpreter execution.

4. **Add a build-only target probe, not a global switch.** Static dispatch currently has no per-PC native/interpreter counters. Add a temporary diagnostic counter or trace at the static target case, plus a build-only way to force only this case to the interpreter. `PSX_NATIVE_BLOCK` and the dynamic overlay counters are insufficient because they do not disable or count a static case. Record target-native, target-interpreter, CRC-miss, bad-entry, and guard-yield counts separately.

5. **Prove parity before measuring speed.** Run the target fragment through a native-versus-dirty-interpreter comparison from identical CPU/RAM state. Use the existing differential contract where it applies, but for static dispatch require an equivalent target-specific probe because `PSX_OVERLAY_DIFF` is in the dynamic loader path. Require 32 consecutive clean target executions, no register/HI/LO/RAM/PC divergence, no device-touch execution, no guard at `0x807814D0`, and no native bad-entry event. Then run the authenticated PAL title path far enough to cover the race transition, CD/IRQ activity, SPU production, and save/media boundaries.

6. **Run a controlled PAL x2/100 Hz A/B.** Use the same optimized Release flags, BIOS, input script, render settings, and route in three target-native and three target-interpreter-control runs. Capture `frame_perf`, guest/present latency, audio underruns/overflows, target dispatch counts, static CRC checks/misses, and the final run report. Do not use the `render` FPS line alone as proof of guest cadence.

## Acceptance measurements

The control should reproduce the known full-workload limitation from `docs/HANDOFF.md`: roughly 74–79 guest Hz after PAL x2 with audio underruns. The separate no-netplay/no-rewind title-side probe reaching 99.7–100.2 Hz is a useful ceiling, not the race proof.

Promote the continuation only if all of these hold:

- target fallback is zero after warm-up, or every remaining fallback has an explicit safe reason and is excluded from the claimed native path;
- target-native and interpreter-control traces are equivalent across the 32-pass proof and the race transition;
- three native race runs reach 99–101 guest Hz with no audio underruns/overflows and no target guard/bad-entry event;
- the native build shows a material improvement over the target-interpreter control: at least a 10 percentage-point reduction in guest-budget pressure or enough reduction to cross the 99–101 Hz gate. A gain under roughly 5 Hz is not sufficient evidence that this continuation is the remaining bottleneck;
- the generated delta remains target-sized and does not reintroduce the 192 MB broad static corpus as the proposed fix.

If parity passes but the native A/B remains below 95 Hz or gains less than 5 Hz, stop attributing the deficit to `0x007814D0` and profile the next guest/render/PGXP consumer. Keep the PAL x2 package default-off until the race trace and soak gates pass.

## Compiler and build constraints

Use generic `-O3 -DNDEBUG` as the comparison baseline. The handoff records that bundled-compiler LTO was ruled out and `-march=native` measured about 71–74 Hz versus roughly 75–79 Hz for generic `-O3`; neither is a substitute for reducing interpreter/native dispatch work. The current canonical `build-release/CMakeCache.txt` still contains experimental LTO settings, so the next A/B must use a clean, explicitly recorded Release configuration rather than inheriting that cache.

No isolated test was added under `tools/audits`: the missing proof is a target-specific generated artifact and runtime A/B, not a reusable parser defect.
