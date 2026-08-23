# Bounded validation: `0x807814D0`

Date: 2026-08-22

## Result

The bounded static-generation proof is complete locally; runtime race parity
and performance are not complete.

- Input: the retained `build-release/overlay_captures-8078.json` corpus.
- Exact target-bearing rows: 103.
- Exact native identities: 96 compiled; seven safely skipped; zero failures.
- Structural proof: stable prologue word `0x27BDFFC0`, exact range/CRC guards,
  `func_807814D0`, `block_807814D0`, and a dispatcher case for canonical
  `0x807814D0`.
- Integration: the generated target-only bundle is compiled as an optional
  `psx_overlay_dispatch_highram` translation unit. The dirty-RAM dispatcher
  tries the existing static corpus first, then this exact high-RAM bundle,
  then the normal dynamic/interpreter paths.
- Generator hardening: duplicate deterministic static recipes now fail closed
  if their source diverges and are otherwise emitted once. Local isolated
  fragments are namespaced in the extra bundle so they cannot collide with
  the established static corpus.

## Runtime evidence

The fresh Clang Release executable links and contains the extra dispatcher and
target symbols. A direct title/menu PAL probe reaches the PAL-x2 CRTC and
10-ms gameplay pacer, but did not execute the race continuation: the probe
reported `dispatch/s=0` and no target-specific native count. Guest cadence in
that probe varied around 93–95 Hz and audio underruns remained high.

This does not prove the target is safe or that it fixes race performance. The
next valid experiment must load the user's race save-state (or reproduce the
same deterministic route), capture the target-native path, and compare against
an exact target-interpreter control.

## Required acceptance evidence

1. Restore one race anchor and run native and interpreter-control branches
   with identical CPU/RAM state.
2. Require 32 consecutive target executions with matching registers, HI/LO,
   RAM writes, guest PC, and device-event behavior; reject any bad-entry or
   target CRC miss.
3. Run three native and three control PAL-x2 race windows. Accept only if
   native runs sustain 99–101 guest Hz with no audio underruns/overflows and
   show a material improvement over the control.
4. Keep the target-only bundle bounded; do not replace it with the full
   1,701-record static corpus.
