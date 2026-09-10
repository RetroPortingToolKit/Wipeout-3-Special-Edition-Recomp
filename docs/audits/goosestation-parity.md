# GooseStation/DuckStation parity audit — WipEout 3 SE

Date: 2026-08-22
Scope: the WipEout 3 project at root `892e350`, including the current
working-tree configuration, and the framework gitlinks `psxrecomp` at
`1b860bae` / `recomp-ui` at `7e9dfc5`. Protected runtime, generated, build,
and active PGXP/GPU files were read only.

The comparison is against the relevant DuckStation surface, not against every
DuckStation feature. “Gap” below means either missing user-visible behavior or
an advertised behavior whose WipEout-specific correctness gate is still open.

## Current disposition

| Area | Current evidence | Disposition |
|---|---|---|
| PAL x2 / 100 Hz | Targeted PAL package and title verification exist (`mods/preloaded/packages/wipeout3.enhancement.framerate-pal100/1.0.0/manifest.toml:11-25`, `tools/verify_pal100_title.py:26-64`). | **P0 gap:** selectable, but not release-proven through FMV, audio, and a sustained race. |
| 900% overclock | `[video] cpu_overclock = 900` is staged in `game.toml:47-55`; framework applies the value through its CPU-cycle setter. | Present; no separate feature gap found. |
| PGXP geometry / texture / depth | `CMakeLists.txt:37-40`, `game.toml:52-55`, and the selected PGXP feature in `mods/state.toml:40-46` cover the requested geometry, perspective-texture, depth, and CPU-mode switches. | Present; no duplicate gap reported. |
| Widescreen / ultrawide | 16:9, 21:9, and adaptive choices are exposed (`mods/preloaded/packages/wipeout3.enhancement.widescreen/1.0.0/manifest.toml:21-55`). | **P1 gap:** 21:9/content safety is not proven; a title frustum-widening path is explicitly disabled after packet-arena corruption was observed (`game.toml:70-103`). |
| Texture replacement | `game.toml:56-58` enables the pack path; the handoff records 445 indexed entries, 299 matched, 300 decoded images, 47,919 hits, and zero decode failures (`docs/HANDOFF.md:17-19`). | Playback is present for the measured external pack. Remaining limitation is coverage/authoring compatibility, which DuckStation itself also documents as game-dependent. |
| FMV | MDEC/depth-24 crop and 50 Hz presentation are implemented and focused-tested (`docs/HANDOFF.md:59-64`, `113-118`). | **P0 validation gap:** long FMV/audio playback and the PAL-x2-to-race transition remain open. |
| Fast boot / BIOS skip | `openbios = false`, `bios_hle = true` (`game.toml:39-45`) and the release gate require the BIOS intro-skip policy (`tools/verify_static_release.ps1:21-26`). | Present; no separate feature gap found. |
| CD speed / async loading | Framework packages exist, but the WipEout state selects only 4× host Fast Loading (`mods/state.toml:7-9`, `32-38`). | **P0/P1 gap:** accelerated-load audio and normal-vs-fast card/CD equivalence are not closed; host image preloading is not exposed. |
| Memory cards / save states | `memcard_dir = "saves"` (`game.toml:39-45`); framework has card images, a 12-slot save-state menu, BIOS compatibility checks, and controlled save/load receipts. | Basic feature present. Equivalence under accelerated loading is part of the open load gate, not a missing save-state implementation. |
| Renderer options | Runtime accepts software/OpenGL/Vulkan at framework config level, but the game selects only OpenGL (`game.toml:47-48`) and does not set `offer_vulkan`. | **P1 gap:** Vulkan is buildable but hidden from the launcher; there is no D3D11 backend equivalent. |

## Prioritized gaps

### P0 — Do not claim PAL x2/100 Hz as complete yet

The current working-tree selection enables the PAL package (`mods/state.toml:20-21,
53-56`), but the durable evidence is mixed. The handoff records strong CD/SPU
timing evidence without proving the title’s exact simulation update count
(`docs/HANDOFF.md:22-28`); optimized windowed runs fell to roughly 74–79 Hz
with audio underruns (`docs/HANDOFF.md:43-50`), and the FMV fix still leaves
long-run audio/race verification open (`docs/HANDOFF.md:59-64`). The same handoff
also records the still-open simulation, media-load, and race traces
(`docs/HANDOFF.md:145-149`). The release policy now selects PAL100 by default,
but that selection must not be confused with a passed runtime receipt.

Required closure: PAL x2 from BIOS through title, every race entry path, FMV,
CD/XA, SPU production, input cadence, and sustained presentation must be
captured with zero underruns and a stable 100 Hz gameplay rate. The default
selection is now required by the release profile; the runtime receipt remains
a separate shipping-readiness gate.

### P0 — 4× Fast Loading is selected before its equivalence gate is complete

`mods/state.toml:7-9` and `32-38` select `psx.enhancement.fast-loading` at a
4× host multiplier. The framework package correctly describes this as host
pacing rather than guest timing (`psxrecomp/mods/builtin/packages/psx.enhancement.fast-loading/1.0.0/manifest.toml:1-46`),
but the WipEout handoff records higher accelerated-path underruns, 54 SPU trace
underruns, and missing paired card/CD summaries (`docs/HANDOFF.md:160-170`).
The full route reaches card reads and race activity, but host presentation and
byte-for-byte paired traces remain open (`docs/HANDOFF.md:183-194`); the
`race-load` gate still lacks equal-length, overlapping endpoint artifacts
(`docs/HANDOFF.md:254-282`).

Required closure: run normal and 4× branches from the same post-load savestate,
with PAL100 off, and compare card writes, CD command/sector/IRQ/DMA ordering,
guest fingerprints, SPU timing, and a short post-race continuation. Keep the
feature selected by the release profile, but equivalence unproven until both
`startup-card` and `race-load` pass.

### P1 — Ultrawide is exposed, but WipEout’s visible-world path is not content-safe

The package advertises 21:9 and adaptive output, but `game.toml:70-82` keeps the
legacy wide path (`native_wide = false`). More importantly, the camera-space
`SceneryFrustumClip` widening is commented out because it advances the packet
cursor into racer state and makes start-grid ships snap/freeze
(`game.toml:93-103`). The project handoff therefore still lists widescreen and
ultrawide validation as unproven (`docs/HANDOFF.md:203-215`).

This is more than a missing screenshot: the current 21:9 choice can change the
outer presentation while leaving some extra-FOV scenery clipped, so it is not
yet equivalent to a stable DuckStation widescreen result. Closure requires
4:3/16:9/21:9 captures across HUD, menus, FMV, start grids, all shipped tracks,
and a packet-arena/high-water check before re-enabling any blocked frustum rule.

### P1 — Renderer capability is narrower and partly hidden

The framework parser accepts only `software`, `opengl`, and `vulkan`
(`psxrecomp/recompiler/src/config_loader.cpp:598-607`), while its schema says
Vulkan is experimental and launcher visibility is controlled by
`offer_vulkan` (`psxrecomp/docs/config_schema.md:380-400`). WipEout sets only
`renderer = "opengl"` and never sets `offer_vulkan` (`game.toml:47-48`), so a
Vulkan build is not a selectable product option. The current framework also has
no D3D11 backend. DuckStation’s official settings surface documents hardware
backends alongside software, with Direct3D 11/OpenGL availability and Vulkan
as a supported hardware target.

Closure: expose only backends that have WipEout acceptance evidence, beginning
with software/OpenGL/Vulkan; add a launcher-visible Vulkan option after testing
PGXP, depth, FMV, texture replacement, and wide presentation on it. Treat the
lack of D3D11 as an explicit platform-scope limitation unless the project elects
to add that backend.

### P1 — CD speed is available in the framework, but image-preload parity is absent

The framework has a bounded CD Speed package (`psxrecomp/mods/builtin/packages/psx.enhancement.cd-speed/1.0.0/manifest.toml:15-58`) and an asynchronous
CD/DMA scheduler, but WipEout’s local preloaded catalog contains only the
title-specific PAL and widescreen packages. The current state selects host
Fast Loading, not CD Speed (`mods/state.toml:7-9`), and `game.toml` has no
equivalent of a host image-preload setting. DuckStation separately exposes CD
read speedup, seek speedup, and “Preload Image to RAM”; those are not the same
as asynchronous guest DMA delivery.

Closure: expose a conservative per-game choice for read/seek speedup and/or
host image preload, then verify that FMV/CD audio remain authentic and that
WipEout’s card/load routes do not depend on the accelerated interrupt timing.

## Deliberately not reported as gaps

- The 900% CPU setting is present and wired into the runtime; it is not merely a
  comment or UI label.
- PGXP is enabled through the project build and selected mod, with geometry,
  perspective-texture, depth, and CPU propagation controls present.
- The measured external HD pack is actually substituted at runtime; the audit
  does not relabel that as missing just because DuckStation supports additional
  dump/replacement edge-case knobs.
- BIOS intro skipping, memory-card images, save-state slots, thumbnails, and
  BIOS-scoped state compatibility are implemented. The remaining card/state
  concern is the Fast Loading equivalence gate above.

## Reference baseline

- [DuckStation README — feature and platform baseline](https://github.com/stenzek/duckstation#readme)
- [DuckStation console settings — fast boot, CPU overclock, CD speedup, and image preload](https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/consolesettingswidget.cpp)
- [DuckStation graphics settings — renderer and enhancement semantics](https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/graphicssettingswidget.cpp)
- [DuckStation texture replacement workflow and limitations](https://github.com/stenzek/duckstation/wiki/Texture-Replacement/014974199fab7227e3920618b1ce51c0693cdaeb)
- [DuckStation command-line fast boot behavior](https://github.com/stenzek/duckstation/wiki/Command-Line-Arguments)
