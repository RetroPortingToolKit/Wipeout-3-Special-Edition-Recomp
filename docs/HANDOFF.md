# Handoff

## Current position

The implementation and local release verification are complete on
`codex/fast-pal100-ultrawide` in the `wipeout3se-fast-ultrawide` worktree. The
canonical dirty worktree was not modified.

The title now uses the authenticated stock SCES-02845 executable plus guarded
direct mods. It no longer applies Hueponik's xdelta/AOT overlay catalogs or the
high-RAM replacement engine. Reviewed defaults are PAL100, native 8 MiB RAM,
adaptive widescreen, Draw Far, Max ship LOD, ShipDist 65K, Birds High, HD
textures/fonts, and startup-intro skip. Track 4x4 and PGXP remain off.

The final Release executable is
`build-release-lto-shot/Wipeout_3_Special_Edition__Recompiled.exe`, SHA-256
`1e4ce03daf40dce77f6ac04b4907fb53cf2d0afdf4845e5994559b7c794c2252`.
It contains no progress/race-clock diagnostic plugins or shipping callbacks.

## Final acceptance evidence

The authoritative speed proof is the stock in-race HUD timer in renderer-native
PresentShot PNGs, bound to monotonic `Stopwatch` timestamps. FPS is excluded.
The final matrix is
`artifacts/final-shipping-soak/final-release-visible-timer-matrix.json`:

| Aspect | Client | Visible-timer aggregate | Moving pairs |
| --- | ---: | ---: | ---: |
| 4:3 | 1440x1080 | 0.993761x | 4/4 |
| 16:9 | 1920x1080 | 1.006297x | 4/4 |
| 21:9 | 2520x1080 | 1.004562x | 4/4 |
| 32:9 | 3840x1080 | 1.003012x | 4/4 |

Every individual interval is within 0.95--1.05x and every aggregate is within
0.99--1.01x. All four runs report about 100 Hz guest cadence, about 44.1 kHz
SPU/CDDA, and zero underrun/overflow faults. All 20 images were manually checked
for a legible HD HUD, post-start chase camera, intact geometry, and horizontal-
only widescreen reveal.

The extracted lean ZIP was separately launched at 2520x1080. Its visible timer
read 10.22, 11.20, 12.20, 13.18, and 14.20 seconds; the cryptographically bound
aggregate is 1.001134x and all four intervals pass. That run also reports guest
99.99 Hz, CDDA 44,120.15 Hz, motion 4/4, and zero faults. Receipt:
`artifacts/package-smoke/21x9-race-fixed/20260828-072732-525/visible-race-timer-review.json`.

Equal-height 4:3 and 32:9 images additionally prove identical vertical framing
and horizontal reveal only. The fixed race start uses the intended start pan
and chase camera; the prior trackside-camera regression is gone.

Static/focused verification passes for the exact PAL 25/50/100 selector, all
138 timing words, 40/40 half-phase load sites, exact 8 MiB behavior, Hue visual
presets, intro skip, 292 HD PNGs and 390 mappings including all 20 font atlases,
release hygiene, exact PGXP-free NCLIP, mod/runtime behavior, PresentShot, and
recompiler site hooks. Top-level Debug and Release each pass 6/6 tests; the
Python tool suite passes 51/51.

## Release artifacts

- `dist/wse-0.1.0-win-x64-lean.zip`: SHA-256
  `25db852ca3d39c37a5b9198a5e522dfee071d40e2a05968a8ace1d458a7c322b`,
  136 entries, reviewed `mods/state.toml`, no source/SDK/disc/BIOS payload.
- `dist/wse-0.1.0-win-x64.zip`: SHA-256
  `21e8d51bc6b448ae433589565c64cfec88ebbd77f7cea60d91996aa1a786c9e3`,
  2,035 entries, reviewed `mods/state.toml`, setup SDK included.

The first lean-package smoke correctly failed closed because the package
excluded `mods/state.toml`; the packager now seeds only the reviewed immutable
`mods/preloaded/state.toml`, never a developer's mutable selection.

## Next concrete step

Commit and push the runtime branch, open its upstream PR, commit the title
gitlink and release layer, and fast-forward root PR #8. PR #7 is already closed.

## Ruled-out approaches

- Hueponik xdelta/disc overlays or high-RAM engine: too costly on dynamic code
  and not a direct mod port.
- 400% CPU: the visible timer proved dense race starts slow despite nominal
  cadence counters. 300% also caused wide-aspect audio underruns. 250% passes.
- FPS, scheduler counters, motion, or menu presence as gameplay-speed proof:
  only visible race time versus wall time is accepted.
- Start-pan, flyby, loading, or demo frames as camera/speed proof.
- Track 4x4 and PGXP: explicitly excluded.
- Aspect screenshots fitted to a common preview width: use equal-height raw
  frames to judge vertical scale.
