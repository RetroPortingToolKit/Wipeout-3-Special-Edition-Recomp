# Renderer quality audit

Date: 2026-08-22
Scope: renderer feature coverage that can materially improve the remaster, without changing the active PGXP/GPU implementation.

## Decision summary

The current release path is OpenGL at 4x internal scale, nearest texture filtering, raw color presentation, and the HD replacement pack. It is featureful enough to be a useful visual baseline, but it is not yet a complete hardware-renderer quality profile.

The highest-value follow-ups are:

1. Make texture-replacement lookup and cache behavior measurable and bounded. The pack is working, but the current lookup path still scans live uploads and has no user-visible cache budget or eviction policy.
2. Measure resolution scale and downsampling independently. The current `supersampling` and `antialiasing` controls do not expose the separate box/adaptive downsampling choices available in the DuckStation family.
3. Validate Vulkan as a real opt-in runtime path before treating it as a supported alternative. It is compiled in this checkout, but there is no captured Vulkan launch in the current evidence set.
4. Keep dithering, interlacing correctness, rounded texture coordinates, and PGXP depth as separate renderer/accuracy work items. They are meaningful quality gaps, but changing them here would cross the protected active GPU/PGXP boundary.

D3D backend selection and anisotropic filtering are low-return for this title at present. D3D is a portability project, not a demonstrated visual fix; anisotropy is not a good fit for the current native-texel/manual-bilinear PSX texture path.

## Evidence snapshot

| Area | Current evidence | Interpretation |
| --- | --- | --- |
| Release settings | `build-release/settings.toml`: OpenGL, 4x supersampling, antialiasing on, nearest texture filtering, bicubic FMV, `crt_filter = "raw"`, 16:9 | This is the reproducible baseline to preserve for A/B captures. |
| OpenGL runtime | `build-release/audit-current-1787.out.log`: OpenGL 3.3 NVIDIA, internal 4x pipeline, mask-bit stencil, texture window, GPU copy/upload | OpenGL is the only backend with a demonstrated release capture in the evidence set. |
| Backend build coverage | `build-release/CMakeCache.txt` has `PSX_ENABLE_VULKAN=ON`; `build-release/build.ninja` contains the Vulkan renderer object | Vulkan is build-enabled, but build presence is not runtime parity. There is no D3D backend in the current renderer enum/config surface. |
| HD replacements | Runtime evidence records 445 indexed entries, 299 matched entries, 300 decoded images, 47,919 replacement hits, and zero decode failures | Loading is healthy. The remaining risk is lookup cost, cache lifetime, and content coverage—not basic pack decoding. |
| Timing | The PAL x2 path is configured for 100 Hz, but the paired error log includes samples from roughly 54–84 Hz before later recovery; `render` telemetry counts display-area flips, not swap cadence | Scale/replacement changes need a real presentation-cadence measurement. Do not infer GPU bottleneck from the `render` field alone. |
| Captures | `fmv_probe.png` is a 320x240-style movie frame with no text outside the movie bounds; `race_probe.png` reaches an on-track race with HUD text | These are useful visual anchors for filter, crop, color, and scaling A/B tests, not proof of pixel parity by themselves. |

The broader runtime/timing history is recorded in `docs/HANDOFF.md`. The focused pixel-accuracy audit in `psxrecomp/accuracy/axis5_gpu.md` already documents the missing dithering path and interlace/timing limitations; this document avoids duplicating that implementation audit.

## Coverage matrix

Status labels mean:

- **Covered**: available and exercised in the current release path.
- **Partial**: code or configuration exists, but parity, policy, or runtime evidence is missing.
- **Gap**: no current setting or implementation surface was found.
- **Defer**: potentially useful, but not a good remaster-quality investment until a prerequisite is measured.

### 1. Backend selection

**Current:** The renderer enum/config accepts software, OpenGL, and Vulkan. The facade falls back to software when a selected backend is unavailable. The OpenGL path is the demonstrated path; Vulkan is compiled in the inspected Release build but has no launch/capture evidence. D3D is absent from the current enum and config loader.

**Reference behavior:** DuckStation exposes multiple hardware devices/backends, including OpenGL, Vulkan, and D3D11/D3D12, behind a shared GPU hardware renderer. Its backend/device selection is primarily a portability and driver-coverage feature; it does not automatically make a game look better.

**Action:** **P1 — validate Vulkan before expanding the backend list.** Run the same race/FMV/settings capture on Vulkan, record fallback behavior, frame pacing, texture-replacement hits, depth behavior, and visual diffs. Add D3D only if Windows driver coverage or a measured defect requires it. Do not add a backend speculatively.

### 2. Texture filtering

**Current:** Persistent texture filtering accepts only `nearest` and `bilinear`. OpenGL has optional bilinear sampling and separate present-time FMV filters (`nearest`, `bilinear`, `sharp`, `bicubic`). Vulkan has a nearest sampler plus a shader bilinear branch. There is one texture-filter choice rather than separate polygon and sprite policies, and no JINC2/xBRZ-style high-quality mode.

**Reference behavior:** DuckStation separates polygon and sprite texture filtering, supports additional shader/filter modes, and treats filtering as a setting that can be changed independently of resolution scale.

**Action:** **P2 — add only after content A/B evidence.** Test nearest versus bilinear on HUD text, menu glyphs, race textures, and replacement textures. If bilinear is acceptable, a separate sprite-filter setting is the most targeted extension. High-quality filters should remain opt-in because Wipeout’s authored pixel art and replacement pack may benefit from nearest sampling.

### 3. Rounded texture coordinates

**Current:** No `force_round_texcoords` setting exists in the current config/settings surface. OpenGL has backend-specific UV shifts and limits, but those are not the same as a user-selectable rounded-coordinate policy.

**Reference behavior:** DuckStation exposes a force-rounded-texture-coordinate option, primarily as a targeted workaround when upscaling nearest-filtered artwork. Its shader generation applies the policy conditionally rather than treating it as universally correct.

**Action:** **Defer behind a scene corpus.** This belongs with texture-coordinate/GPU implementation work, not the current remaster tranche. If later pursued, make it an opt-in A/B setting and compare HUD/sprite alignment, texture seams, and replacement-texture edges at 2x and 4x. Do not change the active PGXP path to enable it.

### 4. Depth buffer

**Current:** `game.toml` enables `pgxp_depth`, and the current OpenGL run logs depth/stencil-backed rendering. The OpenGL path has depth state and a stencil mask; the Vulkan source has depth resources, but there is no Vulkan runtime evidence in the current audit. Depth is coupled to the active PGXP/geometry path rather than exposed as an independent remaster quality toggle.

**Reference behavior:** DuckStation has an optional hardware depth buffer used with PGXP and recreates/changes depth resources as the selected mode requires.

**Action:** **Covered for the current OpenGL title path; P1 parity test for Vulkan.** Preserve the existing PGXP/depth configuration. Validate occlusion, coplanar polygons, HUD ordering, and long-race stability on each backend before claiming parity. No depth implementation changes are authorized by this audit.

### 5. Dithering

**Current:** The software and OpenGL renderer comments explicitly identify dithering as absent. The GP0(E1) dither bit is decoded into GPU state but is not forwarded through the current draw path. There is no dither setting in `settings.toml`; `crt_filter = "raw"` is a present-time color expansion, not original PSX hardware dithering.

**Reference behavior:** DuckStation exposes dithering modes and scaled-dithering policy, and its generated hardware shaders receive dithering/scaled-interlacing flags.

**Action:** **P1 accuracy backlog, outside this change.** The visual benefit is potentially high in gradients and translucent effects, but implementation requires protected GPU/shader changes in both renderers. Track it separately from renderer selection and validate with gradient/transparency fixtures before changing a default.

### 6. Interlacing and deinterlacing

**Current:** CRTC/GPU state contains interlace-related state, but the settings surface has no display deinterlacing mode or scaled-interlacing policy. The existing accuracy audit identifies field/vblank behavior as heuristic/timing-sensitive. That is different from merely scaling an interlaced frame.

**Reference behavior:** DuckStation separates display deinterlacing mode from scaled interlacing and exposes both as settings.

**Action:** **P2 compatibility backlog, outside this change.** Build a menu/FMV/interlaced fixture first. Then decide whether the missing behavior is a timing fix, a presentation policy, or both. Do not add a superficial toggle that masks the current field/vblank limitation.

### 7. Color correction

**Current:** The present path has a 32,768-entry BGR555-to-RGB888 LUT with `raw`, `crt`, `composite`, and `trinitron` screen kinds. The current settings select `raw`. This is a present-time display approximation; 24-bit FMV frames bypass the 15-bit LUT, and the implementation is not a full composite signal/chroma simulation.

**Reference behavior:** DuckStation’s renderer/settings surface treats true-color output and dithering as separate concerns. Its hardware renderer settings are a useful reference for keeping color/pixel-format policy distinct from texture filtering and resolution.

**Action:** **P2 presentation polish.** Keep `raw` as the accuracy baseline. Capture `raw`, `crt`, and `composite` on the same race/FMV frames, and document the intended profile. If a period-display mode is desired, make it an explicit presentation preset and decide whether 24-bit FMV should receive a compatible treatment; do not silently alter the current default.

### 8. Downsampling

**Current:** `supersampling` controls internal scale and `antialiasing` affects the current presentation path, but there is no independent downsample mode or downsample scale setting. The OpenGL path presents the scaled FBO; software keeps a native-resolution texture path plus a high-resolution mirror. This is not equivalent to choosing box, adaptive, or disabled downsampling.

**Reference behavior:** DuckStation has explicit downsample mode/scale settings and maintains downsampling levels separately from the resolution scale.

**Action:** **P1 measurement, P2 implementation.** First capture 2x/4x with HD replacements on/off and nearest/bilinear, separating emulation time, upload/lookup time, and presentation cadence. If downsampling materially improves the replacement pack without harming HUD edges, add it as an independent opt-in present/render profile in a later tranche. Do not silently reinterpret the existing `antialiasing` flag.

### 9. Anisotropic filtering

**Current:** No anisotropy setting or mipmapped PSX texture path exists. Texturing is based on native texel/CLUT access and manual nearest/bilinear behavior; the replacement path follows the same texture-window and UV constraints.

**Reference behavior:** The upstream PSX hardware settings reviewed do not make anisotropy a first-class PSX texture-quality control. The useful upstream controls are texture filtering, resolution scale, downsampling, and cache/replacement policy.

**Action:** **Defer; do not add.** Anisotropy without a deliberately designed mip/LOD path would add complexity and could violate CLUT, texture-window, UV-clamp, or replacement-texture behavior. It is not a demonstrated quality gap for this remaster.

### 10. Texture replacement cache

**Current:** Replacement lookup is backend-agnostic and feeds software, OpenGL, and Vulkan. The current pack has strong decode/hit evidence, but the texture-pack source still maintains a fixed lookup memo and can linearly scan live uploads. The source comments identify lookup cost under high-refresh workloads; partial VRAM overwrites can invalidate an entire tracked upload. There is no user-visible replacement cache budget, preload policy, or eviction telemetry comparable to a dedicated VRAM replacement cache.

**Reference behavior:** DuckStation has a GPU texture cache and a replacement-image cache with bounded cache settings, replacement scale/filter controls, invalidation, and VRAM-write-aware lifetime management.

**Action:** **P0/P1 highest implementation ROI.** Keep the current pack and first add deterministic counters for lookup scans, memo hits/misses, decoded bytes, upload bytes, invalidations, evictions, and per-scene replacement misses. Then replace the hot linear scan with an indexed/bounded structure and add an explicit cache budget/eviction policy. This is the clearest route to reducing replacement-induced stalls and making incomplete content coverage visible. It is future implementation work in protected runtime/GPU-adjacent files, so this audit makes no code change.

### 11. Resolution scaling

**Current:** The config accepts supersampling from 1x through 16x; the demonstrated OpenGL run uses 4x. Vulkan’s current backend setter clamps scale to 1x–4x, while the software renderer uses a native VRAM path plus a high-resolution mirror. There is no single cross-backend capability report or persisted per-backend maximum in the user settings.

**Reference behavior:** DuckStation calculates and clamps resolution scale against device limits and keeps resolution scale independent from downsampling and texture filtering.

**Action:** **P1 baseline selection.** Run a fixed race/FMV matrix at 1x, 2x, and 4x with the HD pack enabled and disabled. Record presentation cadence separately from game/render telemetry, along with replacement lookup/upload costs. Select a title default only from those measurements; do not change the current 4x setting based on one noisy run.

## Prioritized action list

| Priority | Action | Why it matters | Boundary |
| --- | --- | --- | --- |
| P0 | Preserve a reproducible OpenGL baseline and add a capture/metric checklist | Prevents filter, scale, color, and replacement changes from being judged by mixed evidence | Documentation/QA only |
| P1 | Instrument and index texture replacement lookup; add bounded cache/eviction accounting | Current pack works, but lookup scans and cache lifetime are the strongest measured quality/performance risk | Future runtime/GPU-adjacent implementation |
| P1 | Run Vulkan launch/parity captures | Build-enabled does not prove a usable backend; depth, filtering, fallback, and pacing need evidence | Validation first; implementation only if a defect is found |
| P1 | Measure 1x/2x/4x and HD on/off, with separate swap-cadence telemetry | Establishes whether scale or replacement work is the actual limiter | QA/benchmark first |
| P2 | Add independent downsample policy | Offers a likely quality/performance control beyond the current scale-plus-antialiasing coupling | Later renderer/presentation tranche |
| P2 | Add separate sprite/polygon filtering and an opt-in rounded-coordinate policy | Potentially improves replacement/UI edges, but can damage authored pixel art | Later GPU/texture-coordinate tranche |
| P1 accuracy / P2 compatibility | Implement dithering and fix interlace/deinterlace behavior | High visual/compatibility value, but both cross the protected GPU/timing boundary | Separate accuracy work |
| P2 | Document and compare color profiles, including the 24-bit FMV decision | Makes period-display styling deliberate and reversible | Presentation-only follow-up |
| P4 | Add D3D backend only for a measured platform need | Improves portability, not necessarily this game’s image quality | Separate backend project |
| Defer | Anisotropic filtering | No mip/LOD design or demonstrated need | Do not add in current renderer |

## Validation recipe for the next tranche

Use the existing OpenGL release configuration as the control. For each candidate change, capture the same menu, FMV, and on-track frames:

1. Run 1x, 2x, and 4x. Repeat with HD replacements disabled and enabled.
2. Run nearest and bilinear texture filtering. Keep FMV filtering fixed while testing game textures.
3. Run `raw` and one CRT profile separately; do not mix color-profile changes with scale conclusions.
4. For Vulkan, record whether the requested backend actually initializes or falls back, then repeat the same captures.
5. Record replacement entries, matched/decoded counts, hits, misses, decode failures, upload bytes, lookup-scan cost, and eviction/invalidation counts.
6. Treat swap/present cadence as the frame-rate measurement. Keep it separate from display-area flip/render counters and from the game’s PAL/NTSC cadence.

The acceptance set should include HUD glyph sharpness, sprite alignment, texture seams, race geometry/occlusion, FMV crop and bounds, color gradients, transparency, and long-race stability. A setting is not a quality improvement if it only improves a static screenshot while causing replacement churn or cadence loss during a race.

## Reference behavior and source map

The upstream comparison is based on official DuckStation source, which is the most reliable reference for the GooseStation-derived renderer/settings behavior:

- [DuckStation hardware GPU renderer](https://github.com/stenzek/duckstation/blob/master/src/core/gpu_hw.cpp) — resolution scale, filtering, depth, downsampling, texture-cache settings, and renderer checks.
- [DuckStation settings](https://github.com/stenzek/duckstation/blob/master/src/core/settings.cpp) — persisted filtering, dithering, interlacing, rounded-coordinate, replacement-cache, and downsample options.
- [DuckStation hardware shader generation](https://github.com/stenzek/duckstation/blob/master/src/core/gpu_hw_shadergen.cpp) — filter, rounded-coordinate, dither, and interlace shader policies.
- [DuckStation texture cache](https://github.com/stenzek/duckstation/blob/master/src/core/gpu_hw_texture_cache.cpp) — replacement lookup/cache lifetime and bounded cache behavior.
- [DuckStation graphics settings UI](https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/graphicssettingswidget.cpp) — how the quality controls are separated for users.
- [DuckStation D3D11 device](https://github.com/stenzek/duckstation/blob/master/src/util/d3d11_device.cpp) — representative D3D backend coverage.

In-tree source points used for the current-side audit are `psxrecomp/recompiler/src/config_loader.cpp`, `psxrecomp/recompiler/src/config_loader.h`, `psxrecomp/runtime/include/gpu_render.h`, `psxrecomp/runtime/src/gpu_render.c`, `psxrecomp/runtime/src/gpu_gl_renderer.c`, `psxrecomp/runtime/src/gpu_vk_renderer.c`, `psxrecomp/runtime/src/gpu_sw_renderer.c`, `psxrecomp/runtime/src/tex_pack.cpp`, `psxrecomp/runtime/src/color_lut.c`, and `psxrecomp/accuracy/axis5_gpu.md`.

## Scope guard

This audit intentionally makes no changes to PGXP/GPU implementation files, `runtime/src/main.cpp`, `compile_overlays.py`, generated overlays, `CMakeLists.txt`, `game.toml`, or active mods. The deliverable is the prioritized evidence and test plan above; implementation items that cross the renderer boundary should be handled as separately reviewable changes.
