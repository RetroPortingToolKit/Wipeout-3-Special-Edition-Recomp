# Semantic audit: `0x8011FE6C` and `0x80140F9C`

## Result

`0x8011FE6C` is `SpawnRandomizedParticleRecords`, a small game-side ABI adapter
over the private producer `EnqueueRandomizedParticleRecords` at `0x80140F9C`.
The names describe the proven fixed-size effect records and randomized spatial
offsets without assigning an unproven visual subtype.

## Exact boundaries and ABIs

The generated range table establishes these independent owners:

- `SpawnRandomizedParticleRecords`: `[0x8011FE6C, 0x8011FE94)`, `0x28` bytes,
  two blocks, ending with the return delay slot at `0x8011FE90`.
- `EnqueueRandomizedParticleRecords`: `[0x80140F9C, 0x801410C8)`, `0x12C`
  bytes, seven blocks, ending with the return delay slot at `0x801410C4`.

The wrapper's original ABI is effectively:

```c
void SpawnRandomizedParticleRecords(
    void *ignored_owner, int32_t count,
    const int32_t origin[3], const int32_t spread[3]);
```

It never reads original `a0`. It reorders `a1 -> a0`, `a2 -> a1`, and
`a3 -> a2`, then calls the callee. The callee therefore receives
`(count, origin, spread)`. Neither routine establishes a semantic return;
the wrapper merely leaves the callee's incidental final `v0` value intact.

## Callee behavior

For `count <= 0`, the callee returns without touching the pool. Otherwise each
iteration advances the masked write index in the ring descriptor at
`0x801E1B10` and selects a 48-byte record from its base pointer at `+0x0C`.
It initializes:

- `+0/+4/+8`: the three origin words;
- `+16/+20/+24`: independent `NextRandom15() & 0x3F` values plus the
  corresponding signed spread component minus 32;
- `+32/+33/+34` and `+36/+37/+38`: zero;
- `+40 = 8`, `+44 = 1`, `+45 = 8`, and `+46 = 2`.

The descriptor's `+4` write index is updated before each record is populated.
The only outgoing direct calls are three calls per iteration to the confirmed
`NextRandom15` leaf at `0x80104AA0`; there is no allocator or direct renderer
call.

## Callers and interior-target proof

Five generated wrapper calls collapse to four unique guest call sites:

- `0x800EB4D0`
- `0x800EBCBC`
- `0x800EBD04`
- `0x801597E0`, emitted in two overlay images

Each observed caller passes `count = 8` and stack-resident three-word origin
and spread vectors. The owner/context value in original `a0` is intentionally
discarded by the adapter. The only direct call to `0x80140F9C` is the wrapper's
call at `0x8011FE7C`. Exhaustive generated-source searches find no direct call
or jump to an interior block of either owner; overlay duplication does not add
a distinct guest caller.
