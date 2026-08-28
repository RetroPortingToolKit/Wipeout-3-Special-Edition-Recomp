# Handoff

## Current position

The PGXP-free adaptive widescreen title layer is implemented on current
`origin/master`.  Its exact-NCLIP and fractional-DPI fallback runtime dependency is isolated in
[mstan/psxrecomp#247](https://github.com/mstan/psxrecomp/pull/247).  Clean
generation/build, the two-track visual matrix, PAL100/CDDA timing, and controlled
moving soaks are recorded in `docs/WIDESCREEN_RELEASE_VERIFICATION.md`.
The reviewed root layer is
[TechnicallyComputers/Wipeout-3-Special-Edition-Recomp#8](https://github.com/TechnicallyComputers/Wipeout-3-Special-Edition-Recomp/pull/8).
The obsolete broad proposal, PR #7, is closed with a supersession receipt linking
both replacement layers.

## Next concrete step

Review and merge runtime PR #247 first, then merge root PR #8 so its submodule
pin resolves from the upstream runtime branch.

## Open blockers

None.  A verified stock SCES-02845 disc remains a user-owned build input and is
intentionally absent from the repository.

## Ruled-out approaches

- PGXP was rejected because this release must remain PGXP-free and the title's
  failure mode is handled by nine exact, title-scoped NCLIP branch sites.
- A hash bypass was rejected; normal stock-disc validation and generation pass.
- Duplicating the PAL title patch in C was rejected.  The existing declarative
  framerate package owns those bytes, while the new package owns only host
  2x/100 Hz activation.
- Naive full-throttle alternating steering was rejected: it kept telemetry in
  game mode but eventually destroyed the craft.  The final bounded profile uses
  alternating steering plus a 0.1-second throttle pulse every five seconds.
- Time-limited race states were rejected after entering the out-of-time screen,
  even though `game_mode` remained 1.  Final states explicitly save Game Setup
  with Checkpoints OFF through both `[EXIT]` rows.
- Reloading a savestate every 20 seconds was rejected because it collapsed
  pacing to roughly 43 Hz and introduced audio underruns.  Final receipts load
  once before the measured interval.
- The published auto-drive input did not prevent checkpoint timeout and was
  rejected.
- A decrypted checkpoint-time RAM write froze the guest at frame 2459 while the
  host stayed responsive.  The verifier now requires strictly advancing guest
  frame counters; no guest patches are used in final evidence.
- A first Checkpoints OFF attempt used Triangle to leave the menu, which
  discarded the option.  The accepted states use the explicit `[EXIT]` rows.
- A nominal 960x720 client becomes 2161x1620 at 225% DPI on the verification
  host.  Treating its one-pixel overshoot as widescreen was rejected; runtime
  commit `b6aedc80` keeps that rounding pixel in the exact 4:3 identity path.
- An Altima receipt sampled two seconds after savestate restoration accumulated
  294 SPU CD underflow frames and was rejected.  Final Altima receipts take the
  baseline after a 20-second settling interval and add zero audio faults.
- The earlier broad root/runtime proposal is superseded by separate minimal PR
  layers against current upstream.
