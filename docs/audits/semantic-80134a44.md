# Semantic audit: `0x80134A44` / `TestRacerInputFlag2Or20`

## Result

`0x80134A44` is a pure racer-input predicate now named
`TestRacerInputFlag2Or20`:

```c
bool TestRacerInputFlag2Or20(uint32_t input_flags, int racer_index)
{
    if (racer_index == -1)
        return false;

    RacerRecord *record = &gRacerRecords[racer_index];
    uint32_t shift = (record->type_at_02 != 1) ? 4 : 0;
    return (input_flags & (2u << shift)) != 0;
}
```

Thus type 1 racers test bit `0x02`; every other record type tests bit `0x20`.
The function has no rumble-state relationship despite its proximity to the
rumble helpers later in the same utility cluster.

## True boundary and generated split

The generated range table reports a `0x14`-byte entry shard at `0x80134A44`
and a separate `0x4C` range at `0x80134A58`. They are one logical function:

```text
80134A44  addiu v0,zero,-1
80134A48  bne   a1,v0,0x80134A58
80134A4C  move  a2,a0
80134A50  jr    ra
80134A54  move  v0,zero

80134A58  lui   a0,0x8019
80134A5C  addiu a0,a0,0x10E0
80134A60..80134A78  compute racer_index * 0x3A4
80134A7C  addu  v0,v0,a0
80134A80  lbu   v1,2(v0)
80134A84  addiu v0,zero,2
80134A88  xori  v1,v1,1
80134A8C  sltu  v1,zero,v1
80134A90  sll   v1,v1,2
80134A94  sllv  v0,v0,v1
80134A98  and   v0,a2,v0
80134A9C  jr    ra
80134AA0  sltu  v0,zero,v0
```

The entry's taken branch continues directly into `0x80134A58`, passing the
saved flags in `a2`; that shard has neither an independent ABI entry nor a
prologue. The next callable begins at `0x80134AA4`. The complete owner is
therefore `[0x80134A44,0x80134AA4)`, `0x60` bytes.

## ABI, formula, and state

- `a0`: input flag word; only bit `0x02` or `0x20` can affect the result.
- `a1`: signed racer index; exactly `-1` is the no-racer sentinel.
- `v0`: normalized boolean zero or one.

For nonnegative expected indices, the multiply/add sequence addresses
`0x801910E0 + racer_index * 0x3A4`, the established racer/session record
array. No explicit bounds check exists; callers are responsible for supplying
`-1` or a valid index.

The type selection is exactly:

```text
shift = ((record[2] XOR 1) != 0) * 4
mask  = 2 << shift
```

So only byte value 1 selects `0x02`; zero, two, and every other value select
`0x20`. The function performs one byte load and no memory store, call, global
update, or hardware access.

## Complete caller and interior-target census

An exhaustive aligned instruction-word scan of the retained executable finds
exactly eight direct calls:

```text
800EBCA4 800EBCD4 800EBCEC 800F9F5C
8013CAFC 8013CB8C 8014651C 80146B80
```

All eight target `0x80134A44`. There are no direct `j` or `jal` targets to the
generated continuation at `0x80134A58` or any other interior address through
`0x80134AA0`. This proves the split range is not independently callable.

Caller windows consistently load `a0` from runtime-racer byte `+0x15B` and
supply a racer/player index in `a1`; they immediately branch on `v0`. This
supports the input-predicate interpretation and confirms that the return is a
boolean, not a masked flag value.

## Neighbor relationship

The preceding logical predicate at `0x801349EC` follows the same racer-index
sentinel and record-type selection pattern but tests input bit `0x01` for type
1 or `0x10` otherwise. `TestRacerInputFlag2Or20` is its second-bit companion,
not a continuation of it and not a rumble updater.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, all caller windows, and exhaustive aligned target scans;
- `generated/SCES_028.45_full.ranges` for the generated `0x14 + 0x4C` split;
- established racer-record base/stride evidence in existing semantic audits;
- adjacent predicate disassembly at `0x801349EC` for the paired bit-selection
  pattern.
