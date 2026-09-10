# Semantic audit: `0x80135DD8` / `SetMatrixTranslation`

## Result

`0x80135DD8` is the narrow PSX `MATRIX` translation setter now named
`SetMatrixTranslation`:

```c
void SetMatrixTranslation(MATRIX *matrix, int32_t x, int32_t y, int32_t z)
{
    matrix->t[0] = x;
    matrix->t[1] = y;
    matrix->t[2] = z;
}
```

It changes only the three 32-bit translation words and deliberately does not
mark any enclosing game transform as dirty or initialized.

## Exact boundary and ABI

The complete owner is `[0x80135DD8,0x80135DE8)`, exactly `0x10` bytes and four
instructions, matching the generated range table:

```text
80135DD8  sw  a1,0x14(a0)
80135DDC  sw  a2,0x18(a0)
80135DE0  jr  ra
80135DE4  sw  a3,0x1C(a0)    # return delay slot
```

The preceding matrix helper returns at `0x80135DD0` with a NOP delay slot at
`0x80135DD4`. The next independent Euler/matrix builder creates a 0x30-byte
frame at `0x80135DE8`. There is no shared tail or fallthrough.

`a0` is the destination `MATRIX *`; `a1/a2/a3` are signed 32-bit translation
x/y/z. The function is frameless and calls no callee. It never assigns `v0`,
and callers do not consume a scalar result, so the semantic return type is
void.

## Exact effects and layout

The established PSX `MATRIX` prefix is:

```text
+0x00..+0x10  nine 16-bit Q12 rotation elements
+0x12         alignment padding
+0x14         signed 32-bit translation X
+0x18         signed 32-bit translation Y
+0x1C         signed 32-bit translation Z
```

This owner performs exactly three full-word stores. It does not read the old
matrix, transform or scale the inputs, touch rotation/padding, allocate, call
the GTE, or write global state. Source values are copied bit-for-bit; signed
coordinate interpretation comes from callers and the established layout.

## Distinction from `SetTransformTranslation`

`SetTransformTranslation` at `0x80135574` writes the same three offsets in the
larger game transform but also writes signed halfword `1` to enclosing field
`+0x40`. `SetMatrixTranslation` omits that side effect. The distinct helper is
used where callers own plain matrices or deliberately do not want to alter the
larger transform-state marker.

The neighboring `CopyTransformMatrix` at `0x80135CDC` independently proves
that the nine halfwords plus three translation words are the meaningful PSX
`MATRIX` fields and that `+0x12` is skipped padding.

## Complete caller and interior-target census

An exhaustive aligned instruction-word scan of the retained executable finds
exactly 11 direct calls:

```text
800E882C 800E8CA0 800FA898 800FBEEC
80124DF0
80130EB4 80130EE4 80130F9C 80130FC8
80147270 80147DF8
```

All 11 calls target `0x80135DD8`; no direct `j` or `jal` targets
`0x80135DDC`, `0x80135DE0`, or the delay slot at `0x80135DE4`.

Caller windows obtain `a0` from matrix pointers (often a pointer stored at
owner/object offset `+0x20`), prepare world positions or relative offsets in
`a1/a2/a3`, and continue by copying or building matrix rotation. Two setup
paths explicitly pass zero for all three components. None branches on or
otherwise consumes `v0`.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, caller windows, and exhaustive aligned target scans;
- `generated/SCES_028.45_full.ranges` for the exact `0x10` boundary;
- `docs/audits/semantic-80135cdc.md` for independently proven MATRIX layout;
- `docs/audits/semantic-80135574.md` for the distinct larger-transform setter
  and its additional `+0x40` state effect.
