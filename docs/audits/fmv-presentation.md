# WipEout 3 SE FMV presentation audit

Date: 2026-08-22

Scope: PAL x2 gameplay cadence, native FMV cadence, 4:3 movie geometry and
clipping, filtering, ultrawide composition, bezel boundaries, and launcher to
runtime display-mode handoff.

## Result

No isolated presentation-only code change is justified by the current evidence.
The OpenGL path already contains the relevant crop, edge-bleed, short-band,
filter, and bezel-boundary protections. The remaining uncertainty is coverage:
the available FMV image is a 320x240 source/frame probe, not a full-window probe,
and there is no clean long PAL x2 movie soak covering OpenGL, Vulkan, software,
16:9, 21:9, and a very wide physical drawable. This audit therefore records the
current causes and the validation matrix without touching the protected runtime,
mod, or build files.

## Findings

| Area | Current behavior and exact cause | Status |
| --- | --- | --- |
| PAL gameplay at 100 Hz | Native PAL x2 is armed only after the guest programs a PAL display mode; the runtime sets the 10 ms frame period and re-applies presentation cadence in `psxrecomp/runtime/src/main.cpp:2987-3037`. An explicit native rate makes the wall-clock pacer authoritative and forces swap interval 0 in `psxrecomp/runtime/src/main.cpp:2884-2913`. | Code path is correct. The existing `build-release/pal100-long.out.log:38-42` reports PAL x2, 100 Hz, 10.0000 ms, vsync off. The longer `build-verification/pal100-wallfix-long.out:42-60` is not a clean soak: guest rate falls to roughly 91/66/60 Hz while underruns rise. |
| FMV cadence | While MDEC is active and the display is depth-24, PAL x2 presents one of every two depth-24 ticks (`div = 2`) and still runs the guest/audio clock and wall pacing on every tick; the skip return is `psxrecomp/runtime/src/main.cpp:6990-7031`. This is the authored PAL 50 Hz movie cadence over a 100 Hz guest/presentation clock, not a duplicated 100 Hz movie. | Intended behavior is confirmed by code. The focused handoff evidence reports approximately 20 ms between movie swaps in `gl_present_ring`; the auto-skip-enabled `pal100-long` log alone must not be used as FMV proof. |
| FMV 4:3 layout | Depth-24 frames are classified as native 4:3 in `psxrecomp/runtime/src/main.cpp:7183-7210`, and native-wide presentation is explicitly excluded for them in `psxrecomp/runtime/src/main.cpp:7222-7231`. Fixed outer-aspect layout centers a 4:3 content rect inside the selected outer canvas in `psxrecomp/runtime/src/ws_present_layout.c:33-53`. | Pass by inspection. Full-window 16:9/21:9 screenshots are still required. |
| Crop/clip and text escape | Packed RGB upload width is bounded by the tracked depth-24 upload span in `psxrecomp/runtime/src/gpu.c:3099-3125` and `:3233-3270`. The GL CPU presenter shrinks only the draw width when `content_w < src_w`, keeps the image left-aligned, leaves the trailing area black, and applies half-texel UV insets in `psxrecomp/runtime/src/gpu_gl_renderer.c:3891-3955`. | The inspected `fmv_probe.png` is 320x240 and the “PSX PRESENTS” text stays inside its image bounds. This is a source/frame proof, not proof that a host OSD or every decoded frame stays inside the full-window movie rectangle. |
| Scaling/filtering | The OpenGL FMV filter supports nearest, bilinear, sharp, and bicubic; bicubic is the renderer fallback in `psxrecomp/runtime/src/gpu_gl_renderer.c:3385-3410`. The runtime applies the selected filter to the FMV CPU path in `psxrecomp/runtime/src/main.cpp:7451-7480`. SDL software uses nearest for depth-24 or AA-off and linear otherwise in `psxrecomp/runtime/src/main.cpp:7519-7529`. | OpenGL behavior is covered by code and the current active configuration uses OpenGL. Vulkan depth-24 presentation currently passes `0 /* nearest */` in `psxrecomp/runtime/src/main.cpp:7283-7302`; Vulkan filter parity is a validation item, not an isolated fix made here. |
| Ultrawide composition | WipEout’s configuration offers widescreen and ultrawide, while keeping native-wide disabled for this title (`game.toml:60-79`). The runtime only selects the wide compositor for non-depth-24 gameplay frames; FMV/menu frames stay in the canonical 4:3 path in `psxrecomp/runtime/src/main.cpp:7183-7227`. The shared layout helper keeps the outer target aspect separate from the inner 4:3 content in `psxrecomp/runtime/src/ws_present_layout.c:33-53`. | Composition policy is correct by inspection. No usable 21:9 or very-wide full-window capture was found. The disposable `race_probe.png` is visibly corrupted and is not presentation evidence. |
| Bezel boundaries | The GL gameplay path calls `present_bezel` before the gameplay quad, using the exact content rect and margin scissor bands in `psxrecomp/runtime/src/gpu_gl_renderer.c:5391-5515` and `:5665-5673`. The FMV CPU path in `:3859-4008` never calls `present_bezel`; it clears black and draws only the movie rect. Edge-fill is also disabled for depth-24 in `psxrecomp/runtime/src/main.cpp:7200-7202`, and crop mode deliberately disables edge smearing in `psxrecomp/runtime/src/gpu_gl_renderer.c:3969-3994`. | Boundary isolation is intentional and safe: bezel art cannot cover FMV pixels, and FMV edge garbage cannot smear into the bars. If bezel art is expected during FMV, that is a product decision requiring a separate change; it is not inferred here. Full-window bezel screenshots remain outstanding. |
| Launcher/runtime display mode | The launcher advertises FMV filtering and the PSX profile exposes window size, renderer, and FMV filter capabilities in `recomp-ui/src/consoles/psx/psx_profile.h:148-160`. Its fullscreen cycle is Off → Borderless → Exclusive in `recomp-ui/src/common/launcher_model.c:1357-1373`; aspect cycling is intentionally locked when adaptive view is active in fullscreen at `:1068-1083`. The runtime maps 0/1/2 to windowed/borderless/exclusive SDL flags in `psxrecomp/runtime/src/main.cpp:1280-1287` and applies the setting at window creation in `:13911-13934`. It maximizes only an unspecified-size window and sanitizes host refresh before applying the initial swap interval in `:13941-14002`. | Handoff is structurally correct. Validate all three modes on the target 100 Hz panel; do not assume a borderless desktop mode is an exclusive refresh-mode change. |

## Evidence reviewed

- `build-release/pal50-long.out.log:37-38`: PAL x1 at 50 Hz with a 20.0000 ms wall-clock period.
- `build-release/pal100-long.out.log:38-42`: PAL x2 at 100 Hz with a 10.0000 ms wall-clock period and driver vsync off on the 100 Hz panel.
- `build-release/pal100-long.err.log:18-35`: later aggregate intervals report 100.0 game/render FPS, but the same run enabled automatic FMV skipping, so this is not a movie cadence proof.
- `build-verification/pal100-telemetry.stdout.log:20-21`: PAL x2 at 100 Hz with wall pacing and vsync off while host refresh is unknown.
- `build-verification/pal100-wallfix-long.out:42-60`: negative long-soak evidence; the guest rate degrades and audio underruns accumulate after PAL x2 begins.
- `fmv_probe.png`: 320x240 source/frame probe; decoded title text is inside the frame bounds.
- `race_probe.png`: not accepted as evidence because the captured race image is visibly corrupted/disposable.

These artifacts were inspected from the shared build/capture checkout and were
not copied into this documentation change.

## Validation commands

Run the static checks from the repository root. The first command confirms that
the key path has not drifted; the second extracts the expected cadence banners
from an existing run:

```powershell
rg -n "div = 2|g_mod_native_vblank_active|gl_renderer_present|wide_present|content_4_3" `
  psxrecomp/runtime/src/main.cpp
rg -n "CRTC mode|present cadence|runtime cadence|underruns" `
  build-release/pal50-long.out.log `
  build-release/pal100-long.out.log `
  build-verification/pal100-telemetry.stdout.log `
  build-verification/pal100-wallfix-long.out
```

For a fresh OpenGL pixel capture, use the already-built runner and the normal
PAL x2 launch configuration. `PSX_PRESENT_DUMP` writes source frames;
`PSX_PRESENT_DUMP_OUT=1` also writes the post-filter drawable result. The
`PSX_PRESENT_DUMP_FIRST` and `PSX_PRESENT_DUMP_COUNT` variables keep the run
bounded (`psxrecomp/runtime/src/gpu_gl_renderer.c:3319-3368`):

```powershell
$dump = Join-Path (Get-Location) 'artifacts\fmv-present'
New-Item -ItemType Directory -Force $dump | Out-Null
$env:PSX_PRESENT_DUMP = $dump
$env:PSX_PRESENT_DUMP_OUT = '1'
$env:PSX_PRESENT_DUMP_FIRST = '0'
$env:PSX_PRESENT_DUMP_COUNT = '8'
& '.\build-release\Wipeout_3_Special_Edition__Recompiled.exe'
Remove-Item Env:PSX_PRESENT_DUMP, Env:PSX_PRESENT_DUMP_OUT, Env:PSX_PRESENT_DUMP_FIRST, Env:PSX_PRESENT_DUMP_COUNT -ErrorAction SilentlyContinue
```

Check the image dimensions before judging content placement:

```powershell
python -c "from PIL import Image; from pathlib import Path; [print(p.name, Image.open(p).size) for p in sorted(Path('artifacts/fmv-present').glob('*.png'))]"
```

For cadence and composition, query the runtime TCP debug server while the FMV
is playing. The exact protocol payloads are documented in
`psxrecomp/runtime/src/debug_server.c:9372-9445`:

```json
{"cmd":"gl_present_ring","n":600}
{"cmd":"present_ring","n":600}
```

Expected PAL x2 observations are: CRTC PAL x2 at 100 Hz; wall-pacer period
10.0000 ms; CPU/depth-24 movie swaps near 20 ms; `present_ring` entries marked
native 4:3; and no FMV entries marked native-wide. Repeat the capture with
launcher fullscreen values Off, Borderless, and Exclusive; then repeat the
OpenGL capture at 16:9 and 21:9. A separate Vulkan run should explicitly record
that FMV filtering is nearest until filter parity is addressed.

## Follow-up gate

The audit is presentation-pass by code inspection and limited capture, but not
release-pass for ultrawide/full-window content. Close that gate only after a
clean 16:9 and 21:9 FMV drawable capture shows the 4:3 movie centered, no guest
pixels or text outside the movie rect, no trailing packed-RGB smear, and bezel
art confined to gameplay margins; and after the PAL x2 FMV ring shows the native
movie cadence without the audio-underrun pattern in
`build-verification/pal100-wallfix-long.out`.
