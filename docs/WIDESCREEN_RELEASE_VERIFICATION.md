# Direct PAL100 and ultrawide release verification

Verification date: 2026-08-28

## Shipped behavior

- Authenticated stock SCES-02845 title image; no hash bypass.
- Direct Hue-compatible PAL 25/50/100 selector, default PAL100.
- Direct native 8 MiB RAM and title initialization.
- Draw Far, Max ship LOD, ShipDist 65K, and Birds High.
- Track 4x4 excluded; PGXP geometry, CPU mode, depth, and perspective texturing
  disabled.
- Adaptive 4:3, 16:9, 21:9, and 32:9 support with exact NCLIP sign handling.
- 292 HD PNG replacements and 390 mappings; all 20 font atlases active.
- Only the authenticated startup movie calls are skipped. The disc's five
  STR/MDEC streams are all startup movies.
- CPU default 250%, selected from visible-title-time and audio evidence.

## Visible race-time matrix

Each row is an independent Release run from boot into a live race. Five exact
renderer PresentShot frames show the post-start chase camera and stock race HUD.
The HUD readings were manually transcribed, each PNG was rehashed, and game-time
deltas were compared to monotonic capture timestamps. Every interval must be
0.95--1.05x and each aggregate 0.99--1.01x; FPS is not evidence.

| Aspect | HUD readings (seconds) | Aggregate | Guest | CDDA | Faults |
| --- | --- | ---: | ---: | ---: | ---: |
| 4:3 | 10.28, 11.24, 12.24, 13.20, 14.22 | 0.993761x | 99.99 Hz | 44,075.05 Hz | 0 |
| 16:9 | 10.24, 11.24, 12.22, 13.22, 14.22 | 1.006297x | 99.99 Hz | 44,088.1 Hz | 0 |
| 21:9 | 10.22, 11.20, 12.20, 13.22, 14.22 | 1.004562x | 100.01 Hz | 44,063.4 Hz | 0 |
| 32:9 | 10.08, 11.06, 12.06, 13.06, 14.08 | 1.003012x | 99.98 Hz | 44,031.4 Hz | 0 |

Matrix receipt:
`artifacts/final-shipping-soak/final-release-visible-timer-matrix.json`.

Manual review of all 20 frames found a normal chase camera, intact HUD/fonts,
no wedges/bands/corruption, and horizontal reveal only. Equal-height 4:3 and
32:9 frames have the same HUD and billboard Y/height, with only one-pixel
horizontal rounding.

## Extracted-package proof

The final lean ZIP was extracted into a new directory and its contained
executable was driven through the same 2520x1080 race schedule. Visible HUD
readings 10.22, 11.20, 12.20, 13.18, and 14.20 produce a 1.001134x aggregate;
all four intervals pass. Guest cadence is 99.99 Hz, SPU 44,097.0 Hz, CDDA
44,120.15 Hz, motion 4/4, and audio faults zero.

The run proves the package selects the reviewed default mod state with SHA-256
`5ef9f0c36fbf7fbe7411b31630adf44394fd8d8003bfa88dce1c0478a273c8d6`.
Receipt:
`artifacts/package-smoke/21x9-race-fixed/20260828-072732-525/visible-race-timer-review.json`.

## Build and static gates

- Clean Release and Debug generation/build: pass.
- Top-level Release and Debug tests: 6/6 each.
- Runtime GTE, PresentShot listener/writer, mod packages/runtime, and recompiler
  patch focused tests: pass.
- Python verification suite: 51/51.
- Direct PAL audit: 100 guarded sites, 138 owned timing words, 8 classified
  non-timing words, and 40/40 half-phase load sites: pass.
- HD audit: 292 PNGs, 390 mappings, 20 required font atlases: pass.
- Intro skip and shipping-catalog hygiene: pass.
- Generated shipping executable and catalog contain no retired diagnostic
  plugin/site references.

## Artifacts

| Artifact | SHA-256 |
| --- | --- |
| Release executable | `1e4ce03daf40dce77f6ac04b4907fb53cf2d0afdf4845e5994559b7c794c2252` |
| Lean player ZIP | `25db852ca3d39c37a5b9198a5e522dfee071d40e2a05968a8ace1d458a7c322b` |
| Full setup ZIP | `21e8d51bc6b448ae433589565c64cfec88ebbd77f7cea60d91996aa1a786c9e3` |

Both ZIPs contain the reviewed default state. The lean ZIP has 136 entries and
contains no source tree, SDK, disc image, or BIOS. The full setup ZIP has 2,035
entries and includes the rebuild SDK without user media.
