# Semantic audit: `0x8014B6A4`

## Result

`0x8014B6A4` is `DivideSignedByPowerOfTwoTowardZero`, a five-instruction
high-fanout arithmetic leaf.  The generated range exposes an internal shared
return suffix as a false callable split.

## Boundary and false split

The unique entry prefix is generated as `R 8014B6A4 14`, covering
`0x8014B6A4..0x8014B6B7`.  Its positive-input branch targets `0x8014B6B8`,
where `jr ra` and its `srav` delay slot occupy eight more bytes.  The next
independent owner starts at `0x8014B6C0`.

Thus the source-level callable entered at `0x8014B6A4` has semantic extent
`[0x8014B6A4,0x8014B6C0)`.  Generated `F/R 8014B6B8 8` describes the internal
positive return suffix, not an independently used API: exhaustive scanning
finds zero direct JALs to `0x8014B6B8`.  It is cataloged as a confirmed block,
not a second function.

## ABI, arithmetic, and return

The recovered ABI is:

```c
int32_t DivideSignedByPowerOfTwoTowardZero(int32_t value, uint32_t shift);
```

For positive values, the suffix returns arithmetic `value >> shift`.  For zero
or negative values, the prefix computes:

```c
-((-value) >> shift)
```

This removes the negative rounding bias of an ordinary arithmetic right shift,
so the result is signed division by a power of two with truncation toward zero.
As specified by MIPS `srav`, only `shift & 31` affects the result.  Negation and
the final result use ordinary 32-bit two's-complement wrapping; there is no
overflow trap or saturation.  `a2`/`a3` are unused.  The result is returned in
`v0`, and the leaf reads/writes no memory or hardware state.

## Exhaustive callers

Generated executable control flow contains exactly 53 JALs to the real entry,
grouped by owner:

- `0x800E39E4`: `800E4088`
- `0x800E8E14`: `800E901C 800E9034 800E904C`
- `0x800E9510`: `800E978C 800E97A4 800E97BC`
- `0x800EA0C8`: `800EA274 800EA28C 800EA2A4`
- `0x800EB4F8`: `800EB8C4 800EB8DC 800EB8F4 800EB910 800EB928 800EB940 800EBA28 800EBA40 800EBA58 800EBA74 800EBA8C 800EBAA4`
- `0x800F04E8`: `800F0614 800F0630 800F064C 800F13B8 800F13E4 800F1410 800F1548 800F1578 800F15A4`
- `0x800F15E4`: `800F1638`
- `0x800F1680`: `800F1BD0 800F1BFC 800F1C28 800F1C88 800F1CB8 800F1CE4 800F1D10`
- `0x800F1D20`: `800F1D44`
- `0x800F1D5C`: `800F1E88 800F1EB4 800F1EE0 800F1F98 800F1FC4 800F1FF0`
- `0x800F8CD4`: `800F8DD4`
- `0x8014DF60`: `8014E77C 8014E7A8 8014E7D4 8014E874 8014E8A0 8014E8CC`

The grouped list totals 53.  Two aligned words in non-code data at
`0x80174D0C/0x80174D24` decode numerically as JAL opcodes to this address but
are not generated call sites; excluding them is necessary for the callable
count.  No other external J or JAL targets an address
inside the semantic extent; the only interior entry is the owner's own branch
to its positive suffix.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS
  arithmetic and exhaustive entry/interior scans;
- `generated/SCES_028.45_full.ranges` for the adjacent `14`/`8` split and next
  owner at `0x8014B6C0`;
- generated call owners for grouping all 53 executable sites and rejecting
  the two opcode-shaped data words.
