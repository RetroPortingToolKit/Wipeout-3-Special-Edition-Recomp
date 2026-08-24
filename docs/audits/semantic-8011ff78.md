# Semantic audit: `0x8011FF78` / `SpawnRacerType1ParticleBurst`

## Result and exact boundary

`0x8011FF78` is `SpawnRacerType1ParticleBurst`, a thin game-side entry for the
private racer particle producer at `0x8013CA90`. The generated range table
proves `[0x8011FF78, 0x8011FF98)`, exactly `0x20` bytes and two blocks. It has
a 24-byte frame, calls the producer without changing any argument register,
returns at `0x8011FF90`, and restores the frame in the delay slot at
`0x8011FF94`. The large independent particle update owner starts at
`0x8011FF98`.

## ABI and wrapper behavior

The delegated producer consumes only `a0`, as a racer index. No other incoming
argument contributes to its branches or stores, and neither producer nor
wrapper establishes a stable scalar result. The semantic ABI is:

```c
void SpawnRacerType1ParticleBurst(int racer_index);
```

The wrapper performs no normalization, reordering, fixed-argument insertion,
global access, or error handling. Its only effect is the direct call to
`0x8013CA90`; all observable behavior belongs to that callee.

## Delegated particle behavior

The exact private producer is `[0x8013CA90,0x8013CE10)` (`0x380` bytes). It
uses `racer_index` to resolve session/runtime racer state and calls the
adjacent racer eligibility predicates before emission. Accepted paths advance
the masked write index of the 48-byte particle ring descriptor at
`0x801E1B10` and initialize one or two records from racer position/motion data
plus repeated `NextRandom15` samples.

The produced records set bytes `+0x2C` and `+0x2D` to 1, clear `+0x2E`, seed
position/vector/color fields, and use the same 48-byte layout recovered for
the randomized particle producer. Rejected racer/session states return before
record publication. This establishes a racer type-1 particle burst without
claiming a narrower visual such as sparks, smoke, or debris.

Relative to adjacent `SpawnType2ParticleEffect`, this wrapper does not accept
explicit position/vector/RGB arguments and does not route through
`EnqueueEffectInstance`; it delegates racer-derived type-1 construction to the
larger private owner. Both record families are consumed by the downstream
particle renderer associated with the recovered three-`TILE_1` packet pool.

## Caller and target proof

Exactly one call targets the wrapper entry, at `0x80156CA4`. The caller passes
its racer index in `a0` immediately after the damage-event path and ignores
any return value. The wrapper has no overlay duplicate. Exhaustive generated
searches find no call or jump to its interior continuation `0x8011FF88` or any
other interior instruction; the sole outgoing call targets `0x8013CA90`.
