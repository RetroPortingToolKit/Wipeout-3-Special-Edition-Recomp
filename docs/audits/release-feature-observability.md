# Release feature observability audit

Date: 2026-08-22  
Scope: launcher/package/state wiring, staged one-folder output, generated
runtime configuration, and the GooseStation comparison surface. Runtime core,
shared config, and generated sources were not modified.

## Executive result

The requested features are represented in two different control planes:

| Feature | Release source of truth | What is actually observable | Release disposition |
| --- | --- | --- | --- |
| 900% overclock | `game.toml` `[video].cpu_overclock = 900` | Staged `game.toml`; framework applies the scalar through its video config | Static gate now requires the exact value |
| PAL x2 / 100 Hz | PAL target manifest + `wipeout3.framerate.pal-x2` plugin + title-side authenticated bytes | Manifest and `verify_pal100_title.py`; no proof of sustained simulation/audio/race timing | Auto-selected by release policy; runtime proof remains open |
| 8 MB RAM | Framework built-in package, selected by launcher state in a release | `mods/state.toml` and staged built-in package; source checkout has no built-in submodule payload | Verify from the staged artifact, not this checkout alone |
| Full PGXP | `geometry_correction`, `perspective_texturing`, `pgxp_depth`, `PGXP` CMake surface, and PGXP package | Staged config plus PGXP manifest | Static gate now requires all three booleans |
| Widescreen / ultrawide | `[widescreen]` and WipEout package choices (`16:9`, `21:9`, `adaptive`) | Config, package manifest, and launcher state | Wired; ultrawide content safety remains unproven |
| HD assets | `[video].hd_textures = true` plus user-supplied external pack | Staged config and runtime pack telemetry | Enabled, but replacement coverage is not universal |
| Skip-FMV | `auto_skip_fmv = false` plus skip-FMV package | Staged config and package state | Must remain user-controlled; no automatic runtime claim |

## Silent fallback and contradiction findings

1. The source checkout does not contain `mods/state.toml` or framework built-in
   packages. Those are generated/staged by the launcher/packager. A source-tree
   inspection can therefore report a feature as absent even when a release has
   it, while a malformed staged release can silently fall back to framework
   defaults. The one-folder verifier is the correct boundary for state and
   payload presence.
2. `CMakeLists.txt` stages only `game.toml` and optional `game_options.toml`,
   then copies `mods/preloaded`; it does not author a state file. State is
   launcher-owned. Any release receipt must include the staged `mods/state.toml`.
3. The PAL manifest and release state are now explicitly default-on by policy.
   That selection is intentionally separate from runtime proof: the static
   verifier accepts only the exact active catalog, while the PAL title and
   gameplay receipt still must prove sustained 100 Hz behavior before shipping.
4. The PAL title verifier authenticates the media hash, executable hash, and
   selector words (`selector 2 -> gSimVblankSkip=1, pal_x2_state=1`). It does
   not prove that the runtime selects the PAL path, maintains 100 Hz through
   FMV, or completes a race without audio underruns. The handoff records those
   gates as open.
5. `auto_skip_fmv = false` is a deliberate source-level default, but the
   one-folder state can still enable the package. Both values must be reported
   when auditing a release; neither one alone proves the user's effective
   setting.
6. HD texture enablement proves only that the replacement subsystem is on.
   Existing telemetry (299 matched entries, 300 decoded images, 47,919 hits,
   zero decode failures in the measured run) is runtime evidence for that pack,
   not a guarantee that every asset has an HD replacement.
7. Widescreen's `21:9` and adaptive choices are exposed, but the dangerous
   camera-frustum widening sites remain disabled because of packet-arena
   corruption. Output can therefore be wide while outer scenery remains
   clipped. This is a known fallback, not full ultrawide parity.

## GooseStation semantic comparison

GooseStation's Skip Engine is not equivalent to this project's Fast Loading or
FMV toggle. Its generic CD detector requires pure data loading, no XA/CDDA,
no MDEC, no controller input, and a primitive-count niceness bound; accepted
frames are executed hidden with SPU output muted and presentation suppressed.
Memory-card skipping has its own access window and debounce/cap rules. A
host-pacing multiplier, a BIOS intro skip, or `auto_skip_fmv` cannot be used as
evidence for those semantics. In particular, requiring normal audio during a
GooseStation hidden interval would be the wrong test, while allowing a WipEout
Fast Loading route to hide XA/MDEC content would be unsafe.

The existing `docs/audits/goosestation-skip-engine.md` is the detailed source
comparison and acceptance contract. Its required telemetry (detector source,
start/stop/cap event, per-frame media predicates, endpoint fingerprint, and
post-skip audio recovery) is not currently emitted by the WipEout runtime;
this audit does not add that runtime instrumentation.

## Verifier changes in this audit

`tools/verify_static_release.ps1` now fails a staged release unless it carries:

- exactly `cpu_overclock = 900`;
- all three full-PGXP booleans;
- `offer_ultrawide = true`;
- explicit `auto_skip_fmv = false`; and
- a PAL x2 manifest whose `default_enabled` is true, matching `mods/state.toml`.

These are narrow release-boundary checks. They do not change runtime behavior,
package state, launcher defaults, generated code, or shared framework config.

## Required release receipt

For a defensible one-folder release, retain the verifier output together with
the staged `game.toml`, `mods/state.toml`, all selected package manifests,
`verify_pal100_title.py --json` output, and the runtime evidence described in
`docs/HANDOFF.md`. Treat a missing state file, missing built-in package, PAL
default-on manifest, or absent runtime trace as an observable release failure,
not as permission to infer the feature from source configuration.
