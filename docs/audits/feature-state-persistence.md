# Feature-state persistence audit

Date: 2026-08-22  
Target: WipEout 3 Special Edition / GooseStation-compatible runtime  
Scope: persistence and restoration of overclock, PAL x2/100 Hz, 8 MB RAM,
PGXP, widescreen, texture replacements, and Fast Loading. Runtime/core/shared
configuration files were read only; this audit is the only intended change.

## Verdict

The current product has two different kinds of state:

1. Launch/profile state comes from `game.toml`, `mods/state.toml`, and selected
   package options. It is reapplied when a session or rematch initializes; it is
   not written into the savestate as a complete settings snapshot.
2. Guest/emulator state comes from the framework savestate. The documented
   compatibility envelope protects the selected mod plan and live RAM size,
   then restores RAM and repairs high-RAM registration. It does not serialize
   every effective setting listed in this audit.

Therefore same-profile save/load is supported and evidenced, but cross-profile
loads are only partially guarded. In particular, a state can restore stale
title RAM containing menu/timing choices while the host runtime has a different
effective timing or presentation configuration. This is the central continuity
risk; it is not evidence that the runtime silently changes the user’s TOML.

## Evidence matrix

| Feature | Configuration source / launch behavior | Savestate coverage | Finding |
|---|---|---|---|
| CPU overclock | `game.toml:47-55` requests `cpu_overclock = 900`; the hardware audit records the framework setter and startup log (`docs/audits/hardware-parity.md:60-69`) | No serialized overclock field; documented compatibility compares mod fingerprint and RAM length only (`docs/audits/hardware-parity.md:107-116`) | **Partial.** A 900% state can load under a changed CPU setting if other checks match. |
| PAL x2 / 100 Hz | Separate PAL package/plugin and title-side selector; release manifest/state select it by default (`mods/preloaded/packages/wipeout3.enhancement.framerate-pal100/1.0.0/manifest.toml:1-25`); runtime timing is distinct from CPU overclock (`docs/audits/pacing.md:44-57`) | No evidence of a serialized PAL multiplier, native host rate, or title selector identity beyond the mod-plan guard | **Partial.** Host CRTC/pacer mode is launch state; title timing bytes are guest RAM/media state. |
| 8 MB RAM | Builtin feature is selected through mod state; high RAM is applied before `memory_init()` (`docs/audits/hardware-parity.md:44-56`) | `BS_SEC_RAM` stores live RAM length and rejects 2 MB/8 MB mismatch; restore resynchronizes high pages (`docs/audits/hardware-parity.md:107-112`) | **Guarded.** This is the strongest feature-specific protection. |
| PGXP/depth | Enabled in `game.toml:52-55`; build selection is recorded in `docs/audits/goosestation-parity.md:18-20` | No feature/settings section is identified in the save-state evidence | **Launch/profile state only.** Same selected runtime is required for meaningful restore. |
| Widescreen/aspect | Package exposes `16:9`, `21:9`, and adaptive options (`mods/preloaded/packages/wipeout3.enhancement.widescreen/1.0.0/manifest.toml:21-55`); game config keeps widescreen game-owned (`game.toml:60-79`) | No aspect choice is identified in the savestate envelope | **Launch/profile state only.** A restored guest does not prove that the current presentation choice matches the one used when saved. |
| Texture replacements | `game.toml:56-58` enables the pack; measured substitution is documented as 299 matched / 300 decoded / 47,919 hits (`docs/audits/renderer-quality.md:25-27`) | No replacement-pack identity, decoded cache, lookup memo, or cache-generation field is identified | **Launch/profile/cache state only.** The pack is rediscovered/rebuilt at launch; a state does not make replacement assets portable. |
| Fast Loading | Selected as host pacing, not guest timing (`docs/audits/goosestation-parity.md:51-61`); equivalence remains an open gate | Savestate protocol has a committed `savestate_status` receipt, but this synchronizes the operation rather than serializing Fast Loading (`docs/HANDOFF.md:245-253`) | **Partial.** Restore is testable, but normal and accelerated branches require separate equivalence tests. |

## What is actually persisted

The durable project evidence says the state writer places `BS_SEC_MODSET`
first, containing the resolved mod-plan fingerprint. It also writes
`BS_SEC_RAM`, containing the live RAM byte count. A load with a different mod
plan or RAM size is rejected before an unsafe restore; after RAM restoration,
high-page bookkeeping is repaired. These are the documented checks in
`docs/audits/hardware-parity.md:107-116` and are corroborated by the same-profile
artifacts named at `docs/audits/hardware-parity.md:35-41`.

The runtime debug protocol’s `savestate_status.generation` is a completion
receipt: it changes after save/load has been applied or rejected, and reports
`pending`, `last_ok`, `last_op`, and `last_slot` (`docs/HANDOFF.md:245-248`).
That protocol prevents a harness from racing a staged restore. It is not a
settings persistence mechanism.

`game_options.toml` currently contains only comments and an example, with no
`[[option]]` rows (`game_options.toml:1-13`). Thus there is no project-declared
native in-game OPTION mapping whose values can be audited as persisted
configuration. The real widescreen option is package-declared, not a native
game-option row.

## Stale timing and menu restoration

The title-side timing choice is represented in guest memory, not solely by the
host PAL package. The generator documents that the in-game frame-rate menu
writes the simulation-skip value, that recomp-specific VBlank handling can
make the stock menu label misleading, and that the menu/title path can reopen
the every-VBlank behavior (`tools/gen_hueponik_framerate_mod.py:63-76`). The
PAL audit likewise identifies selector state and `gSimVblankSkip` as necessary
evidence; a VBlank count alone is insufficient (`docs/audits/pacing.md:44-51,
131`).

Consequently, restoring a state captured after a menu choice restores the
guest RAM bytes that encode that choice. If the current host session has been
started with a different PAL x2/native-rate selection, the result can be a
mixed state: the host pacing/CRTC policy comes from the current launch profile,
while the title’s skip/menu state comes from the checkpoint. Conversely, a
state captured in a PAL-x2 title path can retain the title’s timing selector
when loaded into a stock-PAL session unless the mod-plan compatibility check
rejects the load. The existing evidence does not show a dedicated post-load
reconciliation that rewrites title menu state from the current config.

This explains “stale timing/menu restoration” without claiming an unobserved
runtime bug: it is the natural consequence of serializing guest RAM while
leaving host settings outside the serialized compatibility key. The risk is
highest for PAL x2/100 Hz and the in-game frame-rate menu; it is lower, but
still present as a presentation mismatch, for aspect ratio, PGXP, replacement
pack selection, and Fast Loading.

## Verification and remaining tests

Read evidence confirms:

- same-profile 8 MB + 900% save/load artifacts are recorded in
  `docs/audits/hardware-parity.md:35-41`;
- RAM-size mismatch and mod-plan mismatch are expected to reject in
  `docs/audits/hardware-parity.md:107-116`;
- PAL stock/x2, NTSC x2, SPU, and CDDA clock checks passed in the focused test
  described at `docs/audits/hardware-parity.md:140-145`;
- the load harness now waits for the committed savestate receipt, rather than
  sleeping for an assumed duration (`docs/HANDOFF.md:245-253`).

No new runtime test was run because the requested change is documentation-only
and the framework sources are gitlinked/protected in this checkout. The next
focused matrix should create one checkpoint in each of these states, restore
it into every other state, and record accept/reject plus post-restore values:

```text
8 MB + 900% + PAL x2 + widescreen + PGXP + replacements + Fast Loading
2 MB + 100% + stock PAL + 4:3 + PGXP off + replacements off + normal loading
```

The acceptance rule should be explicit: either serialize and validate every
effective setting that changes guest behavior, or reject cross-profile loads.
For PAL timing, also capture the title selector / `gSimVblankSkip` value after
restore and compare it with the active package; do not use VBlank telemetry
alone as proof of menu or gameplay restoration.

## Scope boundary

This audit did not edit runtime core, shared config parsing, generated code,
PGXP/GPU implementation, package manifests, or active mod state. It records
the current persistence contract and the stale-state risk so a later runtime
change can be reviewed against a concrete test matrix.
