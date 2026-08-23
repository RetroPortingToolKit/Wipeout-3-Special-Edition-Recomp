# Hardware parity audit: 8 MB RAM and 900% CPU

Date: 2026-08-22
Target: WipEout 3 Special Edition recompiled build
Scope: GooseStation-compatible main-RAM and CPU-overclock behavior, including title-hack assumptions, startup/reset, save states, and silent fallbacks.

## Verdict

The currently staged profile applies both requested settings in the guest/runtime:

- `game.toml` sets `[video].cpu_overclock = 900`.
- `mods/state.toml` explicitly enables `psx.enhancement.8mb-ram` / `8mb-ram`.
- Startup logs contain both `psxrecomp: CPU overclock 900%` and `psxrecomp: unique 8 MB main RAM ...`.
- `psx_last_run_report.json` records `last_store_pc = 0x80780AF8`, inside the enhanced high-RAM code region, so the guest did execute relocated enhancement code rather than merely carrying an unused host allocation.

The implementation is GooseStation-compatible in the relevant semantic sense, but the two settings have different enforcement strength. The 8 MB feature makes the high window unique and is activated before `memory_init()`. The 900% setting scales retired CPU instruction charges only; device clocks, CRTC timing, SPU timing, and CD timing remain on their normal cycle domain. That is the appropriate model for a CPU overclock and is separate from the PAL x2 / 100 Hz title-timing feature.

The intended high-detail title profile is therefore:

```text
8 MB unique main RAM + 900% CPU overclock + the selected high-detail/title patch
```

The source and patch generator document that Enhanced/Full WipEout variants need 8 MB, while the top-level configuration documents 900% as required by Hueponik's MAX-detail path. However, the mod resolver does not encode a dependency between the RAM feature, the title patch, and `cpu_overclock`. A user can create a partially compatible configuration without a resolver error.

Current audit status: **settings applied; guest use evidenced; same-profile save/load evidenced; full hardware/title parity not proven**.

## Evidence matrix

| Area | Evidence | Result |
|---|---|---|
| Static profile | `game.toml`, `[video].cpu_overclock = 900` | 900% is requested by the current profile. |
| RAM selection | `mods/state.toml`, `psx.enhancement.8mb-ram` and `8mb-ram` enabled | 8 MB is explicitly selected; the builtin feature is otherwise disabled by default. |
| Runtime application | `runtime/src/main.cpp` applies the overclock before the runtime starts; mod plugins run before `memory_init()` | Both settings reach the live runtime. |
| Live log | `build-release/savestate-continuity.out.log` and `savestate-load-current.out.log` | Logs report 900% and unique 8 MB RAM during startup. |
| Guest high-RAM use | `build-release/psx_last_run_report.json`, `last_store_pc = 0x80780AF8` | Relocated enhancement code in the high-RAM range actually executed. |
| CPU model | `runtime/src/psx_cycles.c`, `runtime/include/psx_cycles.h` | 900% reduces CPU retirement charges; peripherals are not sped up. |
| Guest clock boundary | `build-runtime-tests/guest_clock_test.exe` | PAL stock, PAL x2, NTSC x2, SPU, and CDDA timing checks all passed. |
| Title-hack provenance | `tools/gen_hueponik_framerate_mod.py` | `ntsc8`, `ntscfull8`, `pal8`, `palfull8`, and 120 Hz 8 MB variants explicitly require 8 MB. |
| Static release boundary | `tools/verify_static_release.ps1 -BuildDir .\build-release` | Passed; no release-boundary fallback was reported. |
| Save state | `savestate-continuity.*`, `savestate-load-current.*` | Same-profile save/load succeeded under the 8 MB + 900% profile. |
| Native execution caveat | `psx_last_run_report.json` | A high-PC instruction guard fallback remains; this is not a RAM/overclock fallback, but it prevents claiming complete native performance parity. |

## How 8 MB RAM is applied

`psxrecomp/runtime/src/mod_builtin_ram.c` registers the `psx.8mb-ram` activation plugin. Its callback calls `psx_mod_set_main_ram_8mb(1)`. The public plugin comment in `runtime/include/mod_plugins.h` requires this activation before `memory_init()`.

`runtime/src/memory.c` keeps an 8 MB host backing array but starts each session in the retail 2 MB mode. The activation request changes the live size to `PSX_RAM_8MB` and registers the high range as unique. The mapping helpers in `runtime/include/psx_ram.h` implement the DuckStation-style split:

- registered low and high pages are unique physical RAM;
- unregistered high addresses fold to the low 2 MB alias;
- code-address canonicalization can still fold aliased code PCs for AOT lookup.

The launcher clears the RAM-size request, resolves the selected mod plan, activates its plugins, and only then calls `memory_init()` (`runtime/src/main.cpp`). This makes the setting deterministic for a cold launch and prevents a prior session's request from leaking into a new session. `memory_init()` prints the unique-8-MB diagnostic when the live size is greater than 2 MB.

The current high-PC execution record in `psx_last_run_report.json` is the useful guest-side proof: the high region was not just allocated on the host; the enhanced code path reached it. The report also has `autocompile_degraded = 0`, but its `interp_unsupported` entry records an instruction guard at `0x80781474`. That guard is a code-generation/dispatch limitation, not evidence that RAM silently fell back to 2 MB.

## How 900% CPU is applied

`recompiler/src/config_loader.h` defaults `runtime_cpu_overclock` to 100. `recompiler/src/config_loader.cpp` reads `[video].cpu_overclock`. In `runtime/src/main.cpp`, any non-100 value is passed to `psx_set_cpu_overclock()` and the readback is logged.

`runtime/src/psx_cycles.c` clamps values below 100% to 100% and values above 6400% to 6400%. For 900%, the Q16 scale is the reciprocal CPU-charge scale (approximately 1/9), with a carried fractional remainder. The cycle header and implementation make the boundary explicit:

- CPU instruction retirement is charged through the overclock scale;
- device service is charged in the real-cycle domain;
- CRTC, SPU, CD-ROM, and other peripheral cadence are not multiplied by 9;
- changing the setting resets the overclock accumulator.

This is compatible with the intended GooseStation behavior for a CPU overclock. It should not be interpreted as a 900% game-frame, audio, or PAL-refresh multiplier. PAL x2 / 100 Hz remains the separate `wipeout3.enhancement.framerate-pal100` package/plugin.

## Title-hack expectations

There are two independent pieces of evidence for the intended pairing:

1. `tools/gen_hueponik_framerate_mod.py` labels the 8 MB Enhanced/Full variants (`ntsc8`, `ntscfull8`, `pal8`, `palfull8`, and the 120 Hz 8 MB variants) as needing 8 MB RAM. The generated high engine is staged at `0x80780000+`, consistent with the high-RAM requirement.
2. The current `game.toml` comment says Hueponik's MAX-detail path requires the GooseStation-equivalent 900% CPU overclock. The same file describes the high enhancement engine and its trampoline roots.

The current PAL x2 package manifest targets the expected WipEout 3 PAL image and supplies the CRTC/title-timing transformation, but it does not declare `psx.enhancement.8mb-ram` as a dependency and does not declare a CPU-overclock requirement. The builtin 8 MB manifest targets `*`, has `default_enabled = false`, and likewise has no overclock dependency.

Therefore the compatibility rule is currently a profile invariant, not a resolver invariant. The current state selects both features, but the runtime will not reject all of these incomplete combinations:

- title patch enabled, 8 MB disabled;
- 8 MB enabled, `cpu_overclock` absent or set to stock;
- a high-detail patch enabled with an unrelated CPU-overclock value.

A resolver-level dependency or validation would be a separate implementation change and is outside this audit because the requested runtime and mod implementation files are protected.

## Startup, reset, and rematch behavior

For a launcher session, the observed order is:

```text
load config/state
  -> resolve selected mod plan
  -> clear prior RAM-size request
  -> activate mod plugins (8 MB request)
  -> memory_init() applies live RAM size
  -> start guest
```

The CPU overclock is applied from the loaded runtime configuration before the guest runs. `psx_cycles_reset_for_boot()` resets cycle counters, deadlines, and service batches, but does not reset the selected overclock scale. Thus a normal soft boot/reset preserves the active overclock configuration. The RAM mode is likewise a session/profile setting; it is not toggled by a cycle-counter reset.

The rematch path re-commits the selected mod plan and reinitializes the guest-side memory path. The relevant invariant is that the RAM-size request is reapplied by the activation plugin before the next `memory_init()`, rather than relying on stale state from the previous image.

## Save-state behavior

The save-state format is protective for RAM mode but incomplete for the CPU overclock:

- `BS_SEC_MODSET` is written first and contains the resolved mod-plan fingerprint. A state with a different selected mod set is refused before mutation.
- `BS_SEC_RAM` stores the live `memory_get_ram_bytes()` length, either retail 2 MB or unique 8 MB. Loading a state whose RAM section length differs from the live session is rejected.
- After restoring RAM, `psx_ram_resync_high_after_restore()` repairs the high-page registration bookkeeping.
- The observed continuity and current-load logs show successful save/load while staying in the same 8 MB + 900% session/profile.

The overclock is not a serialized save-state section, and the state compatibility checks do not compare the saved `cpu_overclock` value with the current config. A state saved at 900% can therefore load under a changed overclock if the mod fingerprint and RAM length still match. That is a real parity/continuity limitation and the most important configuration gap found by this audit.

The existing observed rejection of an old state with an ABI mismatch is healthy format validation, not a RAM or overclock fallback. A 2 MB state loaded into an 8 MB session, or vice versa, should fail the RAM-section length check; a changed 8 MB feature selection should fail the mod-plan fingerprint check first.

## Silent or easy-to-miss fallback paths

| Condition | Runtime behavior | Classification |
|---|---|---|
| `mods/state.toml` is missing | `load_state()` returns success with an empty selection; 8 MB is `default_enabled = false`, so the guest starts in 2 MB mode without a warning | **Silent 2 MB fallback** |
| `8mb-ram` is omitted/disabled | Same 2 MB default path; the unique-8-MB startup line is absent | **Silent 2 MB fallback** |
| `[video].cpu_overclock` is missing | Config default is 100; main skips the setter and its 900% diagnostic | **Silent stock-CPU fallback** |
| CPU value is below 100 or above 6400 | Setter clamps to 100 or 6400 and logs the readback | **Clamped, observable fallback** |
| Selected package has wrong game/image target | Mod commit reports an error and launch is rejected | **Fail closed, not silent** |
| Expected-byte guard for a title patch fails | Runtime logs rejection and boots unmodified | **Logged patch fallback** |
| High relocated code lacks a native overlay/guard | Dispatch can use interpreter fallback; current report has one high-PC instruction guard | **Performance fallback, not hardware fallback** |
| 8 MB and 900% are not mutually declared | Resolver accepts an incomplete pairing | **Configuration validation gap** |

The current release logs do not show the first, second, or third fallback: they show 900% and unique 8 MB. They also do not show a rejected active mod plan. The high-PC instruction guard is observed and should remain visible in performance sign-off.

## Verification performed

The following existing, focused checks were run/read and passed:

```text
tools/verify_static_release.ps1 -BuildDir .\build-release
PASS static-release-boundary

psxrecomp/build-runtime-tests/guest_clock_test.exe
ALL PASS
```

The guest-clock test specifically passed PAL stock at 50 Hz, PAL x2 at 100 Hz, NTSC x2 at 120 Hz, stock handling for a zero multiplier, exact 44.1 kHz guest-clocked SPU output, carried SPU cycle servicing, and native CDDA sector cadence. These results support the separation between the CPU overclock and peripheral/CRTC timing; they do not by themselves prove equivalence to every GooseStation title-hack execution path.

## Follow-up boundary

No runtime, GPU/PGXP, overlay-generation, build, game profile, or active-mod implementation was changed for this audit. The evidence supports shipping the current selected profile as **applied and internally coherent**, with these qualifications recorded for any stronger parity claim:

1. enforce or validate the 8 MB + title-hack + 900% pairing at profile/mod resolution;
2. include the effective CPU-overclock setting in save-state compatibility, or explicitly document that cross-config state loads are unsupported;
3. close or consciously accept the high-PC instruction-guard/interpreter path before claiming complete native performance parity;
4. add a controlled 2 MB/8 MB and 100%/900% A/B proof run if behavioral equivalence—not just application—is required.
