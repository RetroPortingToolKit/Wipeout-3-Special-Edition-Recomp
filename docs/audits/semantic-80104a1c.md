# Semantic audit: `0x80104A1C` / `ComputeRaceFrameRandom15`

## Result

`0x80104A1C` is a pure, argument-free race-random sampling leaf now named
`ComputeRaceFrameRandom15`:

```c
uint32_t ComputeRaceFrameRandom15(void)
{
    uint32_t sum = gSelectedRacerSeed + gRaceFrameCounter;
    uint32_t mixed = (uint32_t)((int32_t)sum >> 1) ^ (sum << 3);
    return mixed & 0x7FFF;
}
```

It produces deterministic per-frame variation from shared race state. Unlike
the neighboring `NextRandom15`, it does not advance or store a private PRNG
state.

## Exact boundary, ABI, and formula

The complete owner is `[0x80104A1C,0x80104A48)`, exactly `0x2C` bytes and
matching the generated range table:

```text
80104A1C  lui   a0,0x8017
80104A20  lui   a1,0x8017
80104A24  lw    v1,0x3A8C(a0)   # 0x80173A8C
80104A28  lw    v0,0x3AEC(a1)   # 0x80173AEC
80104A2C  nop
80104A30  addu  v1,v1,v0
80104A34  sra   v0,v1,1
80104A38  sll   v1,v1,3
80104A3C  xor   v0,v0,v1
80104A40  jr    ra
80104A44  andi  v0,v0,0x7FFF    # return delay slot
```

The preceding owner ends at `0x80104A1C`; the next independent leaf begins at
`0x80104A48`. The routine is frameless, calls no callee, and reads no incoming
argument register before replacing it, giving it a `uint32_t(void)` ABI.

The `addu` wraps the input sum modulo 32 bits. The right shift is arithmetic,
so bit 31 of the sum is propagated, while the left shift discards overflow.
The final mask guarantees a result in `0..32767`. Neither shared input word nor
any other memory is written.

## Input-state evidence

The word at `0x80173A8C` is populated from the selected racer/session record at
`0x80102A84..0x80102A9C` and `0x80102B3C..0x80102B60`. The word at
`0x80173AEC` is cleared during race setup at `0x8012BBF0` and incremented at
`0x801233F4..0x80123400`. Calling the former a selected-racer seed and the
latter a race-frame counter therefore reflects their observed producers, not
just their use in this formula.

The adjacent leaves at `0x80104A48` and `0x80104A74` apply the identical mix
after adding fixed offsets `0xA00` and `0xF00`, respectively. They are separate
deterministic variants rather than continuations of this owner.

## Complete caller and target census

An exhaustive aligned instruction-word scan of the retained executable finds
exactly 13 direct calls, all targeting the true entry:

```text
800EA614
801391AC 80139220
8014C7B8 8014CA20 8014E920
80150B38 80150B58
80155968 80155A44
80159418 801594F4
8015ACB0
```

There are no direct `j` or `jal` targets at any interior address in
`[0x80104A20,0x80104A48)`. Representative consumers mask the result to two,
four, seven, or eight bits, reduce it modulo small counts, or subtract
`0x4000`/scale it to derive race and effect variation. All ignore argument
registers and consume `v0` as the bounded sample.

## Relationship to the stateful generator

`NextRandom15` at `0x80104AA0` uses the same signed-shift/XOR transform but
stores the full mixed word back to private state at `0x80173A94`. This owner
instead mixes two externally maintained race words and performs no store. It
is consequently a deterministic frame sampler, not an alias of the general
stateful generator.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, caller windows, and exhaustive aligned target scans;
- `generated/SCES_028.45_full.ranges` for the exact 0x2C-byte boundary;
- producer windows at `0x80102A84`, `0x80102B3C`, `0x801233F4`, and
  `0x8012BBF0` for shared-input semantics;
- `docs/audits/semantic-80104aa0.md` for the independently proven stateful
  generator comparison.
