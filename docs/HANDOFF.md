# Handoff

## Current position

- The original dirty checkout remains preserved at
  `C:\Users\Shokunin\Documents\ChatGPT\wipeout3se`.
- The canonical cleanroom is
  `C:\Users\Shokunin\Documents\ChatGPT\wipeout3se-cleanroom` on
  `codex/remaster-cleanroom`, rooted at `e217914`.
- `psxrecomp@d56bd024` is now classified as a quarantined, non-shipping route
  runner. Its available history is title-tainted back to the local root and is
  not a clean framework base.
- A generic CPS fallthrough defect was independently reproduced and fixed in
  the legacy runner: a reachable unnamed straight-line successor now publishes
  the architectural next PC instead of surfacing a false `PC=0` exit.
- The deterministic route
  `tests/routes/intro-to-first-race.json` completed all 18 steps / 3,117 guest
  frames and reached a live FEISAR race on Porto Kora. The ignored evidence is:
  - `build-clean-baseline/evidence/intro-to-first-race-receipt.json`
  - `build-clean-baseline/evidence/bridge-route-end.png`
  - `build-clean-baseline/evidence/legacy-race-baseline.json`
- The legacy race baseline proves this is not the product architecture:
  approximately 290 million interpreted instructions, 225 million interpreter
  dispatch fallbacks, zero native overlay dispatches, and a 21.4 ms scene-GPU
  maximum while still paced at 59.94 Hz.
- Sol Ultra selected a dual-lane strangler replacement. New original work lives
  under `clean-aot/`; the legacy lane remains only until each boot/race milestone
  has a deterministic replacement.

## Next concrete step

Integrate and independently review the first clean-AOT tranche: PS-X EXE/region
IR, R3000A semantic oracle, deterministic machine timing, offline executable
variant catalog, provenance/release gates, and renderer-neutral geometry
provenance. Build all focused tests from the canonical cleanroom, then connect
the components behind one clean CMake target without linking the legacy runner.

## Open blockers and unknowns

- No clean-AOT code has yet booted a BIOS or game instruction.
- The full set of executable overlays/epochs on disc remains to be enumerated
  offline. Legacy runtime captures are hints only and may not supply semantics.
- Hardware modules (GPU, SPU, CD-ROM, DMA, timers, SIO, MDEC, interrupts) still
  need independent specifications, implementations, and differential tests.
- 100 Hz, audio exactness, wide projection/culling, PGXP, textures, fonts, and
  game fixes remain unimplemented in the clean lane.
- The local patched Track 01 strongly matches the pinned hueponik target, but a
  source-to-target transformation proof still requires the user's independently
  verified original Redump Track 01.

## Ruled out

- Do not clean up or ship `d56`; its architecture and source history are tainted.
- Do not copy/cherry-pick Claude-era fixes, assets, tests, or conclusions.
- Do not count legacy interpretation, runtime compilation, menu runs, synthetic
  benchmarks, or duplicated presentation frames as static recompilation or
  100-FPS evidence.
- Do not implement 100 Hz as CPU overclocking. The required 100-Hz cadence is
  338,688 CPU cycles and 441 SPU samples per VBlank, with three CDDA sectors
  over every four frames.
- Do not commit disc images, retail BIOS data, patched executables, captures,
  generated C, screenshots, saves, or packaged binaries.
