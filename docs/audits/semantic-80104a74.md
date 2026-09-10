# Semantic audit: `0x80104A74` / `ComputeRaceFrameRandom15OffsetF00`

## Result

`0x80104A74` is a pure offset variant of the race-frame random sampler, now
named `ComputeRaceFrameRandom15OffsetF00`:

```c
uint32_t ComputeRaceFrameRandom15OffsetF00(void)
{
    uint32_t sum = gSelectedRacerSeed + 0xF00u + gRaceFrameCounter;
    uint32_t mixed = (uint32_t)((int32_t)sum >> 1) ^ (sum << 3);
    return mixed & 0x7FFF;
}
```

The fixed offset decorrelates this result from the unoffset and `0xA00`
siblings without mutating any generator state.

## Exact boundary, ABI, and formula

The complete owner is `[0x80104A74,0x80104AA0)`, exactly `0x2C` bytes and
matching the generated range table:

```text
80104A74  lui    v0,0x8017
80104A78  lui    a0,0x8017
80104A7C  lw     v1,0x3A8C(v0)   # 0x80173A8C
80104A80  lw     a1,0x3AEC(a0)   # 0x80173AEC
80104A84  addiu  v1,v1,0xF00
80104A88  addu   v1,v1,a1
80104A8C  sra    v0,v1,1
80104A90  sll    v1,v1,3
80104A94  xor    v0,v0,v1
80104A98  jr     ra
80104A9C  andi   v0,v0,0x7FFF    # return delay slot
```

The previous independent `0xA00` variant ends exactly at `0x80104A74`.
`NextRandom15`, which has different stateful semantics, begins at
`0x80104AA0`. This owner is frameless and has no callee.

No incoming argument is read before its register is overwritten, establishing
a `uint32_t(void)` ABI. Both additions wrap modulo 32 bits; the right shift is
signed/arithmetic, the left shift is modulo 32 bits, and the result is masked
to `0..32767` in the return delay slot. There are no memory writes.

## Shared-state provenance

As established for the unoffset sibling, `0x80173A8C` is loaded from the
selected racer/session record during setup at `0x80102A84..0x80102A9C` or
`0x80102B3C..0x80102B60`. `0x80173AEC` is cleared at `0x8012BBF0` and
incremented at `0x801233F4..0x80123400`, identifying it as the race-frame
counter. This function reads both words but changes neither.

The exact mixing input is evaluated in machine order as:

```text
sum = ((selected_racer_seed + 0x00000F00) mod 2^32
       + race_frame_counter) mod 2^32
```

## Complete caller and target census

An exhaustive aligned instruction-word scan of the retained executable finds
exactly eight direct calls:

```text
801391E0 801392B4 8014CA8C 80150B4C
80150B68 80155A00 801594B0 8015AC6C
```

All eight target `0x80104A74`; there are no direct `j` or `jal` targets at an
interior address in `[0x80104A78,0x80104AA0)`. Callers consume `v0` through
small masks (`3`, `0xFF`), constant-divisor reductions, or subtraction of
`0x4000` followed by scaling. These are bounded variation uses, not status
tests, pointer returns, or an implicit state update.

Several consumers pair this owner with `ComputeRaceFrameRandom15` in the same
record initializer. The `0xF00` offset gives the paired values distinct but
deterministic sequences for the same selected racer and frame.

## Relationship to neighboring generators

- `0x80104A1C` mixes `seed + frame` with no fixed offset.
- `0x80104A48` mixes `seed + frame + 0xA00`.
- this owner mixes `seed + frame + 0xF00`.
- `NextRandom15` at `0x80104AA0` instead updates private state at
  `0x80173A94` and is therefore a true advancing PRNG stream.

The distinct boundary and fixed constant warrant a separate symbol rather than
an alias or interior block for the unoffset sampler.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, all caller windows, and exhaustive aligned target scans;
- `generated/SCES_028.45_full.ranges` for the exact 0x2C-byte boundary;
- producer windows for `0x80173A8C` and `0x80173AEC` documented in
  `docs/audits/semantic-80104a1c.md`;
- `docs/audits/semantic-80104aa0.md` for the independently proven stateful
  generator comparison.
