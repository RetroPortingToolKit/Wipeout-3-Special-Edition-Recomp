# Semantic audit: `0x8014FA4C`

## Result

`0x8014FA4C` is the logical owner now named
`SpawnTrackSectionSurfaceEffects`:

```c
void SpawnTrackSectionSurfaceEffects(TrackSection *section,
                                     const void *effect_config);
```

It selects the section's applicable geometry face and spawns eight randomized
type-2 effects across two face-edge constructions. The exact higher-level
visual subtype is not proven, so the name stops at `SurfaceEffects`.

## True boundary versus generated splits

The callable prologue at `0x8014FA4C` allocates `0x88` stack bytes and saves
`ra/fp/s0..s7`. Its only epilogue is the generated piece at
`0x80150548..0x80150574`, which restores those registers and the same frame.
The next independent prologue begins at `0x80150578`.

The true owner is therefore `[0x8014FA4C, 0x80150578)`, `0xB2C` bytes. The
generated table partitions it as:

```text
8014FA4C +094   prologue and face selection
8014FAE0 +300   first topology/interpolation region
8014FDE0 +16C   straight-line continuation
8014FF4C +5FC   shared continuation, second spawn, loop
80150548 +030   sole epilogue
```

The pieces after the first cannot be called independently: they require the
live `0x88`-byte frame, saved registers, selected face in `s3`, context in
`s7`, loop state in `s6`, divisor 10 in `s4`, and stack vectors initialized by
the entry path.

## Caller and false interior-target proof

There is exactly one real direct JAL to the owner:

```text
80150FF0
```

It passes the current linked track section in `a0` and a configuration object
in `a1`. No executable direct `j` or `jal` enters an interior piece. A raw word
scan does find an apparent `jal 0x8014FDE0` at `0x8002B4F0`, but that source
region decodes as unsupported/non-PSX data (including an `sdc1` word), has no
valid call setup, and is not a genuine executable caller. The branches to
`0x8014FF4C` and `0x8015045C` originate inside this owner and are ordinary
shared continuations.

## ABI, validation, and geometry selection

Only `a0` and `a1` are consumed. The function defines no semantic return value
and its caller ignores `v0`. A null `section` returns through the common
epilogue without spawning anything. `effect_config` is assumed nonnull; byte
`effect_config+0x04` bit `0x40` selects between two face topology formulas.

The active geometry context pointer at `0x801985D4` supplies a 12-byte vertex
array at `context+0x0C` and a 24-byte face/geometry-record array at
`context+0x10`. Starting from signed halfword `section+0x34`, the function
walks records at 24-byte stride until byte `record+0x0F` has bit 0 set. There
is no local bound, so authored data must provide such a record.

Face halfword indices at offsets `+0x00`, `+0x02`, `+0x04`, `+0x06`, and the
next record's leading indices at `+0x18/+0x1A/+0x1C/+0x1E` address vertices.
Each vertex contains signed 32-bit XYZ at `+0/+4/+8`.

## Randomized surface construction

The owner loops exactly four times (`s6 = 0..3`). Each iteration constructs
two effects. For each effect it:

1. calls `NextRandom15` to derive a small signed interpolation selector;
2. selects one of two edge-pair formulas based on config bit `0x40`;
3. subtracts indexed vertex XYZ values and scales differences (`>>7`) into a
   direction vector;
4. linearly interpolates another indexed edge with `selector / 10` into a
   world-space position;
5. makes four more random calls for intensity and option values; and
6. calls `0x8011FF10`, which repackages the position, direction, color/options,
   and fixed fields before invoking the shared effect allocator with type 2.

The second half repeats with another random selector and the complementary
face-edge construction. Consequently one invocation makes eight calls to the
type-2 effect adapter and advances the global PRNG 48 times. Integer vector
math uses ordinary 32-bit subtraction/multiplication and signed division by
the constant 10; no overflow or bounds checks are present.

The fixed spawn arguments include two random 0/8 flags, a random value 2..5,
constant 6, and constant 0. Intensity is randomized in 64..127. These exact
parameters support an effect-emission role, but not a narrower named visual.

## State effects

This owner does not write the track section, face records, or vertices. Its
direct writes are stack-local. Observable shared effects come from advancing
`NextRandom15` and the eight calls through `0x8011FF10` into the shared effect
pool. Failure handling belongs to that downstream allocator; this owner does
not check a result or roll back prior spawns.

## Sources checked

- `disc/SCES_028.45`, respecting its 0x800-byte PS-X EXE header, for direct
  instructions, caller scans, and classification of the false data target;
- `generated/SCES_028.45_full.ranges` and generated split bodies for boundary
  and live-frame continuity;
- `docs/audits/semantic-80104aa0.md` for `NextRandom15` and
  `docs/audits/semantic-801701b4.md` for the active geometry context;
- direct disassembly of adapter `0x8011FF10` and sole caller `0x80150FF0`.
