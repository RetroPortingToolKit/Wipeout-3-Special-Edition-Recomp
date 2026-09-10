# Semantic audit: `0x8011FF10` / `SpawnType2ParticleEffect`

## Result and boundary

`0x8011FF10` is `SpawnType2ParticleEffect`, an adapter from game-side particle
arguments to the shared 48-byte `EnqueueEffectInstance` record producer. The
generated range table proves `[0x8011FF10, 0x8011FF78)`, exactly `0x68` bytes
and two blocks. It has a 48-byte frame, calls only `0x80140438`, returns at
`0x8011FF70`, and restores the frame in the delay slot at `0x8011FF74`.
`0x8011FF78` is a separate owner.

## ABI and repackaging

The recovered ABI is:

```c
int SpawnType2ParticleEffect(
    const uint32_t position[3],
    const void *vector_a,
    const void *vector_b,
    uint8_t red, uint8_t green, uint8_t blue,
    uint8_t subtype_base);
```

The first four arguments use `a0..a3`; `green`, `blue`, and `subtype_base`
come from incoming stack slots `+0x10`, `+0x14`, and `+0x18`. The low bytes of
`red/green/blue` are stored consecutively in a local word-sized color object.
Its fourth byte is not initialized by this wrapper and is not part of the
proven color contract.

It calls `EnqueueEffectInstance` with:

```text
type        = 2
position    = original a0
vector_a    = original a1
vector_b    = original a2
source_word = &local_rgb
parameter   = 1024
subtype     = (subtype_base + 2) & 0xFF
```

No argument is range checked. `EnqueueEffectInstance` always returns 1 and
the wrapper leaves that `v0` unchanged through its epilogue, so the semantic
return is the same success value.

## Particle and TILE_1 context

The callee advances the byte-wrapped 48-byte effect pool referenced by
`0x801E1F14`, copies the position and two vector triplets, stores the packed
color word at record `+0x20`, fixed parameter 1024 at `+0x28`, type 2 at
`+0x2D`, the biased subtype at `+0x2E`, and active byte 1 at `+0x2F`.

The adjacent recovered particle renderer owns a separate 32-record packet pool
at `0x801D4EDC`, with three preconfigured GPU `TILE_1` packets per record and
allocation count `0x80175D40`. The type-2 effect records produced here are the
game-side particle inputs consumed by that path. This establishes a particle
effect adapter, while the wrapper itself neither allocates a TILE_1 packet nor
touches an ordering table or GPU state.

## Callers and target proof

Two static call sites target the entry, both inside the logical surface-effect
owner audited at `0x8014FA4C`:

```text
0x80150018  0x80150528
```

They execute four times apiece, yielding eight type-2 effect submissions per
owner invocation. Both pass randomized color/option values and constructed
position/vector workspaces. No direct call or jump targets interior block
`0x8011FF68` or any other interior instruction, and there is no overlay or
duplicate-body instance.
