# Semantic audit: `0x801349EC` / `TestRacerInputFlag1Or10`

## Result

`0x801349EC` is a pure racer-input predicate now named
`TestRacerInputFlag1Or10`:

```c
bool TestRacerInputFlag1Or10(uint32_t input_flags, int racer_index)
{
    if (racer_index == -1)
        return false;

    RacerRecord *record = &gRacerRecords[racer_index];
    uint32_t shift = (record->type_at_02 != 1) ? 4 : 0;
    return ((input_flags >> shift) & 1u) != 0;
}
```

Type 1 racers therefore test bit `0x01`; every other type tests bit `0x10`.
This is the first-bit companion of `TestRacerInputFlag2Or20`.

## True boundary and split ownership

Generated metadata reports a `0x14`-byte entry at `0x801349EC` followed by a
separate `0x44`-byte range at `0x80134A00`. The two ranges form one function:

```text
801349EC  addiu v0,zero,-1
801349F0  bne   a1,v0,0x80134A00
801349F4  lui   v1,0x8019
801349F8  jr    ra
801349FC  move  v0,zero

80134A00  addiu v1,v1,0x10E0
80134A04..80134A1C  compute racer_index * 0x3A4
80134A20  addu  v1,v1,v0
80134A24  lbu   v0,2(v1)
80134A28  nop
80134A2C  xori  v0,v0,1
80134A30  sltu  v0,zero,v0
80134A34  sll   v0,v0,2
80134A38  srav  v0,a0,v0
80134A3C  jr    ra
80134A40  andi  v0,v0,1
```

The entry's taken branch reaches `0x80134A00` with the address upper half from
its own delay slot. The continuation has no independent ABI setup or prologue.
The next callable begins at `0x80134A44`, so the true owner is
`[0x801349EC,0x80134A44)`, exactly `0x58` bytes.

## ABI, formula, and state

- `a0` is the input flag word.
- `a1` is a signed racer index, with exactly `-1` meaning no racer.
- `v0` is normalized to boolean zero or one.

For ordinary indices, the shift/add sequence selects
`0x801910E0 + racer_index * 0x3A4`, the established racer/session record array.
There is no general bounds check; callers must supply `-1` or a valid index.

The type-dependent expression is exact:

```text
shift = ((record[2] XOR 1) != 0) * 4
return (input_flags >> shift) & 1
```

Only type byte 1 produces shift zero. Any other byte value produces shift four.
The function reads one record byte, calls no callee, and writes no memory or
global state.

## Complete caller and interior-target census

An exhaustive aligned instruction-word scan of the retained executable finds
exactly seven direct calls:

```text
800F7FC0 801392F4 8013CBB0 80145F80
801462B8 8014639C 801464E4
```

All seven target `0x801349EC`. No direct `j` or `jal` targets the generated
continuation at `0x80134A00` or any other interior address through
`0x80134A40`, proving that the continuation is not separately callable.

Caller windows load `a0` from runtime-racer byte `+0x15B` and provide a
racer/player index in `a1` (sometimes from a player-state byte). Every caller
immediately branches on `v0`, confirming the boolean predicate contract.

## Paired predicate

`TestRacerInputFlag2Or20` at `0x80134A44` repeats the same `-1` sentinel,
record lookup, and type-dependent four-bit shift, but starts from mask bit 1:
it tests `0x02` for type 1 or `0x20` otherwise. The two true owners are adjacent
but independent and have disjoint caller sets.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, caller windows, and exhaustive aligned target scans;
- `generated/SCES_028.45_full.ranges` for the generated `0x14 + 0x44` split;
- established racer-record base/stride evidence in existing semantic audits;
- `docs/audits/semantic-80134a44.md` for the independently proven paired
  second-bit predicate.
