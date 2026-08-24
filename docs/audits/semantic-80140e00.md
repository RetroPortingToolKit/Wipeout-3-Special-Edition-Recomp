# Semantic audit: `0x80140E00` / `DoesObjectMaskMatchRacerView`

## Result

`0x80140E00` is a pure object-mask/view predicate now named
`DoesObjectMaskMatchRacerView`:

```c
bool DoesObjectMaskMatchRacerView(const uint32_t *object_mask,
                                  int racer_index);
```

It combines the current scene-selection mask with one low bit chosen from the
racer record's type, ignores the opposite low bit in the object mask, and
returns whether all remaining bits match exactly. Its four callers use false
to skip renderer/effect cases.

## Exact owner and ABI

The generated range `[0x80140E00,0x80140E70)` is exact. The entry is frameless
and the sole return is `jr ra` at `0x80140E68`, with boolean normalization in
the delay slot. `0x80140E70` begins a distinct 120-byte-frame routine. Internal
targets `0x80140E3C`, `0x80140E48`, and `0x80140E4C` are branch/jump
continuations, not ABI entries.

- `a0` is a pointer whose first word is the candidate object's selection mask.
- `a1` is a racer/view record index.
- `v0` is normalized to zero or one.

There are no null, negative-index, or upper-bound checks. The routine calls no
subroutine, writes no memory, and changes no global or hardware state.

## Exact predicate

The shift/add sequence computes the established racer-record address:

```text
record = 0x801910E0 + racer_index * 0x3A4
```

The only racer field read is byte `+2`. The complete formula is:

```c
if (record->type_at_02 == 1) {
    ignored_object_bit = 2;
    required_view_bit = 1;
} else {
    ignored_object_bit = 1;
    required_view_bit = 2;
}

return ((*object_mask & ~ignored_object_bit) ==
        (*(uint32_t *)0x801762CC | required_view_bit));
```

This is exact equality, not a conventional nonzero intersection test. Bits
other than the single ignored low bit must match the current scene mask. The
same racer-type distinction independently selects alternate input bits in the
audited racer predicates at `0x801349EC` and `0x80134A44`, supporting its role
as a racer/view variant selector.

## Complete caller and interior-target census

An exhaustive aligned executable scan finds exactly four direct calls:

```text
8011F6D4 8011F874 8011F930 8011FB60
```

All four occur in separate opcode cases of the same large renderer/effect
dispatcher. Each loads `a0` from the current owner pointer at `0x801D4B0C`,
loads `a1` from byte `+1` of the active viewport record published at
`0x801A5840`, calls this predicate, and branches to the common case exit when
the result is false. True continues into geometry position setup and packet or
effect submission. No external JAL or direct jump targets an interior address.

The current-mask global is also used by the audited visible-object and scene
section submission paths. Those routines temporarily manipulate its low bits
around view-specific rendering, which is consistent with this helper's exact
mask comparison. `DoesObjectMaskMatchRacerView` is therefore a conservative
descriptive name, not a claim about the original identifier.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact
  disassembly, the four caller windows, and exhaustive direct/interior scans;
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_full_42.c` for the exact owner and internal blocks;
- `docs/audits/semantic-80134a44.md`,
  `docs/audits/semantic-801415f4.md`, and
  `docs/audits/semantic-801422fc.md` for independent racer-type and current
  scene-mask semantics.
