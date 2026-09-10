# GooseStation/DuckStation PGXP deep audit — WipEout 3 SE

Date: 2026-08-22  
Scope: the WipEout 3 project at `ea84ae6`, framework gitlink `psxrecomp`
`1b860bae0497a1d5ac8b0259d8a9a0f23b073c0e`, and the checked-out GooseStation
builder based on DuckStation `a233ec1f`. This is an evidence audit only. No
runtime, shared configuration, generated overlay, or active mod was changed.

## Executive finding

The project has a coherent OpenGL PGXP *umbrella* configuration: the build
requests `PGXP`, `game.toml` enables geometry correction, perspective
texturing, and PGXP depth, and the selected PGXP package is present. That is
not equivalent to GooseStation's complete PGXP control surface. GooseStation
has independent controls for geometry, culling, texture correction, color
correction, depth, vertex cache, CPU mode, projection precision, tolerance,
2D exclusion, transparent depth, and depth-clear policy. The WipEout profile
does not expose or record most of those controls.

The current release claim should therefore be: **PGXP geometry, perspective
texture, and depth are configured for the demonstrated OpenGL path; complete
GooseStation PGXP parity and race-state safety are unproven.**

## Evidence baseline

| Surface | Evidence | Finding |
| --- | --- | --- |
| Build inclusion | `CMakeLists.txt:37-40` passes `PGXP` to `psxrecomp_add_game_runtime` | PGXP code is selected at build time, but this is not a per-option runtime receipt. |
| Product configuration | `game.toml:47-58` selects OpenGL, 900% CPU, geometry correction, perspective texturing, PGXP depth, 4x scale, and HD textures | The intended baseline is explicit and internally coherent. |
| Wide presentation | `game.toml:63-103` offers 16:9/21:9/adaptive presentation, precise N-clip, and keeps `native_wide = false`; the frustum-widening rule is disabled after packet-arena corruption | Wide output is a presentation/mod path, not proof that PGXP culling and projection remain safe. |
| Backend evidence | `docs/audits/renderer-quality.md` records OpenGL runtime evidence; Vulkan is build-enabled but has no WipEout launch/capture receipt; D3D is absent from the current framework surface | PGXP/depth parity is only demonstrated on OpenGL. |
| Texture pack | `docs/HANDOFF.md:17-19` records 445 indexed, 299 matched, 300 decoded, 47,919 hits, and zero decode failures | HD replacement playback is live, but this does not prove PGXP texture-coordinate correctness. |
| Save-state protocol | `docs/HANDOFF.md:245-246` records a monotonic `savestate_status` generation receipt | Restore application is observable, but no existing receipt proves PGXP cache/precision state is restored identically. |
| Existing gap inventory | `docs/audits/goosestation-parity.md` and `docs/audits/renderer-quality.md` already classify Vulkan, wide content safety, depth coverage, and replacement-cache behavior as incomplete | This audit narrows those gaps to PGXP pipeline stages and race-state tests. |

## Control-by-control comparison

| GooseStation/DuckStation control or stage | Current WipEout status | Audit disposition |
| --- | --- | --- |
| PGXP enable / geometry | Build flag and `geometry_correction = true` are present | **Configured; verify visually and in telemetry.** The umbrella switch does not prove every primitive uses corrected coordinates. |
| Perspective texture correction | `perspective_texturing = true` is present | **Configured, not isolated.** Test track polygons, billboards, HUD sprites, and HD replacements separately; no current evidence distinguishes corrected from affine UVs. |
| PGXP depth buffer | `pgxp_depth = true`; OpenGL depth/stencil evidence exists | **Partial.** Validate occlusion, coplanar track pieces, translucent effects, HUD ordering, and depth reset. No Vulkan receipt exists. |
| PGXP CPU mode | GooseStation exposes `goosestation_pgxp_cpu`; its builder describes all-instruction PGXP versus memory-operation tracking | **Unrepresented.** `cpu_overclock = 900` is not PGXP CPU mode. The project must not call the overclock equivalent. |
| Vertex cache | GooseStation exposes `goosestation_pgxp_vertex_cache` and describes screen-position lookup as an alternative to memory tracking | **Unrepresented.** No WipEout setting or evidence says which equivalent is active. This is a compatibility/performance uncertainty, especially across repeated race draws. |
| Precise colors | GooseStation exposes `goosestation_pgxp_color_correction`, described as perspective-correct color interpolation | **Unrepresented.** Current color LUT/presentation settings are not PGXP color correction. `renderer-quality.md` correctly treats display color and original dithering as separate concerns. |
| PGXP culling | GooseStation exposes `goosestation_pgxp_culling` | **Unrepresented and high risk.** WipEout has separate widescreen cull/N-clip hooks. The disabled frustum widening and prior start-grid packet corruption show that culling cannot be assumed safe from geometry correction alone. |
| Texture correction policy | GooseStation exposes a separate texture-correction switch | **Merged/implicit.** The profile enables perspective texturing but cannot A/B texture correction independently, so regressions in UVs, texture windows, or replacement textures cannot be attributed cleanly. |
| Subpixel / projection precision | GooseStation exposes preserve-projection-FP and its PGXP family includes subpixel-sensitive precision behavior | **Unrepresented.** No project setting or artifact records projection precision, vertex snapping, or subpixel policy. Do not infer this from 4x supersampling. |
| Geometry tolerance | GooseStation exposes a tolerance fallback for bad precise geometry | **Unrepresented.** There is no title-specific tolerance receipt for known WipEout bad polygons or a fallback counter. |
| 2D exclusion | GooseStation exposes `PGXP Disable on 2D Polygons` | **Unrepresented.** WipEout mixes race geometry, sprites, HUD, menus, and FMV transitions. A blanket PGXP claim is unsafe without proving 2D primitives remain stable. |
| Transparent depth | GooseStation exposes a transparent-depth test | **Unrepresented.** WipEout translucency, weapon/effect sprites, and HUD layers need explicit ordering tests. |
| Depth-clear threshold | GooseStation exposes a depth-clear threshold for Z-fighting mitigation | **Unrepresented.** No current WipEout profile records a threshold or a per-scene depth-clear event. |
| Reset | GooseStation's PGXP state is reset with emulation/renderer state; its builder also resets boot-renderer state | **Needs test receipt.** Reset must clear prior-track precision, depth, cache, texture, and widescreen state before a new race. |
| Savestate | GooseStation's builder explicitly forces PGXP state into serialized memory states when required; its PGXP vertex cache is regenerated rather than treated as ordinary guest RAM | **P1 gap.** The WipEout receipt proves save/load application, not PGXP-equivalent continuation. Test save during race, restore, and compare the next deterministic frames. |
| Backend | GooseStation/DuckStation supports software plus hardware renderers; the checked builder includes OpenGL/Vulkan paths and the upstream baseline includes D3D11/D3D12/Metal | **OpenGL only proven.** Software is a useful control; Vulkan is a required parity branch before claiming hardware-backend parity; D3D is outside current project scope. |
| Widescreen | WipEout offers 16:9/21:9/adaptive and precise N-clip, but keeps native-wide disabled and frustum widening disabled | **P1 content-safety gap.** Test PGXP geometry, culling, depth, HUD, and start-grid packet high-water together at 4:3/16:9/21:9. |
| HD textures | `hd_textures = true` and live pack evidence exist | **Playback covered; PGXP interaction partial.** Confirm replacement UVs stay attached to corrected polygons and that texture-cache invalidation survives reset/restore. |

## Pipeline-stage interpretation

The relevant pipeline is not one switch:

`GTE/CPU coordinate production -> PGXP precision/cache lookup -> projection and
subpixel policy -> primitive culling/N-clip -> UV/color interpolation -> depth
test/write -> texture replacement lookup -> backend presentation`.

The current evidence is strongest at the configuration and final OpenGL
presentation ends. It does not instrument the middle boundaries. In
particular:

1. A 4x image and stable OpenGL depth resource do not prove corrected GTE
   coordinates reached every draw.
2. Perspective texturing does not prove PGXP vertex association, texture-window
   handling, or HD replacement UVs are correct.
3. Widescreen projection and culling can change visibility before depth testing;
   the known packet-arena failure makes this a concrete WipEout risk.
4. A successful save-state generation receipt does not prove that transient
   PGXP precision/cache data produces the same post-restore draw stream.
5. Backend build coverage is not backend runtime coverage. Vulkan must be
   launched and captured; fallback to software must be recorded as failure, not
   parity.

## WipEout race-state test matrix

Use the same authenticated PAL disc, profile, input trace, and post-load state
for every branch. Capture the first 300 frames after each checkpoint and retain
the `savestate_status` generation, backend initialization, packet-arena high
water, PGXP/depth counters if available, replacement hit/miss counts, and
present cadence. A visual screenshot alone is insufficient.

| Test ID | State and branches | Pass criteria |
| --- | --- | --- |
| R1 | Boot -> main menu -> single-race setup; OpenGL, software control, and Vulkan requested | Correct backend is reported; no silent fallback; menus/HUD are stable; reset leaves no prior PGXP state. |
| R2 | Same start-grid savestate, 4:3 vs 16:9 vs 21:9/adaptive | No missing track geometry, ship snapping, packet-arena overrun, or HUD drift; cull/N-clip changes are bounded and repeatable. |
| R3 | Start grid -> first corner -> high-speed track section | Track edges, barriers, ships, billboards, and road seams remain stable; no affine texture swim, depth fighting, or near-plane pops. |
| R4 | Race with HD pack on/off at 1x/2x/4x | Correct replacement attachment and UVs; no replacement misses caused by corrected coordinates; cadence and upload/lookup cost are recorded separately. |
| R5 | Transparent weapon/effect sprites, HUD, and finish overlay during race | Transparent depth and 2D ordering match the control; HUD is not occluded by PGXP depth; no sprite warping. |
| R6 | Save during race, advance 120 frames, restore, replay identical input for 300 frames | Post-restore frame hashes/telemetry and visible geometry match the pre-save replay within documented nondeterministic presentation limits; generation receipt advances exactly once. |
| R7 | Reset from an active race, immediately load a different track/race, repeat R3 | No old vertex-cache coordinates, depth contents, replacement bindings, cull policy, or wide projection leak into the new race. |
| R8 | Long race / 3 laps, OpenGL and Vulkan | No progressive drift, cache growth, depth contamination, replacement churn, or presentation underruns; all laps retain correct occlusion and culling. |
| R9 | Toggle PGXP-equivalent branches where available in a GooseStation reference run: geometry, CPU, vertex cache, culling, texture, color, depth | Produce a documented A/B table. Any setting with no WipEout equivalent remains a declared parity gap, not silently mapped to an unrelated setting. |

## Prioritized closure

**P0 — preserve the current OpenGL baseline.** Keep the existing configured
profile as the control and do not relabel CPU overclock, supersampling, color
LUT, or HD replacement enablement as PGXP sub-controls.

**P1 — close state and wide-race safety.** Run R2, R5, R6, and R7 first. The
known widescreen packet-arena failure and the lack of PGXP save-state evidence
are higher-risk than missing cosmetic toggles.

**P1 — prove backend behavior.** Run software control, OpenGL baseline, and a
real Vulkan launch. Record initialization/fallback, depth, texture replacement,
and race results separately. Do not claim GooseStation hardware parity from a
build artifact.

**P2 — add observable option mapping in a later implementation change.** If
runtime work is authorized later, map explicit title policy for culling,
texture correction, color correction, vertex cache, CPU mode, projection
precision, 2D exclusion, transparent depth, and depth thresholds. Each should
have a logged effective value and a race fixture before becoming a release
default.

## Reference source map

- Project configuration: `CMakeLists.txt:37-40`, `game.toml:47-103`.
- Existing parity evidence: `docs/audits/goosestation-parity.md`.
- Existing renderer/backend evidence: `docs/audits/renderer-quality.md`.
- Existing runtime state continuity: `docs/HANDOFF.md:245-246`.
- GooseStation option wiring and descriptions: checked-out
  `goosestation-builder/src/duckstation-a233ec1fb7f46181e77e6d2a52446ad6f0a7c54f/goosify.sh`,
  especially the option bridge around lines 16995-17020 and the option
  descriptions around 19650-19725.
- GooseStation PGXP save-state treatment: the same builder's PGXP state
  changes around lines 2451-2487 and serialization path around 21930-21935.
- Upstream feature baseline: [DuckStation README](https://github.com/stenzek/duckstation#readme)
  and [DuckStation graphics settings](https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/graphicssettingswidget.cpp).

## Scope guard

This document intentionally does not change `psxrecomp`, runtime GPU/PGXP
files, generated overlays, `CMakeLists.txt`, `game.toml`, `mods/state.toml`, or
active packages. It records current configuration, evidence, incomplete
mapping, and the race-state acceptance work required for a stronger parity
claim.
