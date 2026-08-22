# WipEout 3 SE modern-PC refactor acceptance

This document defines new-product evidence. Historical clean-AOT and earlier
remaster receipts are explicitly non-authoritative.

## Startup options

- The launcher exposes **Skip opening sequence**. It skips only the startup
  logos/intro and arrives at the same first interactive title/menu state as a
  normal launch.
- The normal path remains available. In-game FMVs are not globally disabled by
  the startup option.
- Equivalence is proved from clean processes using the same BIOS, disc, card,
  settings, and build. At the first interactive state, compare guest RAM after
  excluding independently justified clocks/counters, CPU/GTE state, active
  overlays, VRAM, SPU RAM/registers, CD state, card bytes, and pending events.

## Memory-card startup acceleration

- Acceleration is optional and never changes the card protocol visible to the
  game: command bytes, response bytes, status flags, checksums, insertion
  state, and final card image must match normal speed.
- Empty, formatted, valid-save, full-card, corrupted-save, invalid-checksum,
  missing-card, write-protected/error, and hot-removal cases are compared.
- The accelerated and normal paths must reach byte-identical first interactive
  state except for an explicit allowlist of monotonic host/presentation clocks.
- Any unrecognized transaction or mismatched intermediate/final digest falls
  back to normal timing before observable divergence.

## Race-load acceleration

- Acceleration may remove waits and host presentation/audio output during a
  recognized loading interval, but it does not substitute assets or bypass
  game logic.
- Disc command/response ordering, sector payloads, decompressed bytes, overlay
  identities, RAM/VRAM writes, SPU/CDDA state, events, and race-start state must
  match the normal path.
- Compare every circuit and prototype track, every race mode, both players,
  cold/warm cache, retry/error paths, and save-dependent unlock states.
- Unknown loads or any state mismatch fail closed to normal timing.

## Evidence rules

- A screenshot or successful boot is smoke evidence only.
- Each acceleration has a deterministic normal-speed oracle, accelerated run,
  structured state receipt, byte-level diff, and repeated-run determinism
  check.
- Disabled tests, skipped routes, inferred coverage, and historical receipts do
  not count as passing evidence.
