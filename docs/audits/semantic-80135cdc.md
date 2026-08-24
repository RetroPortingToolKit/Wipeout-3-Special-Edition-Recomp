# Semantic audit: `0x80135CDC`

## Result

`0x80135CDC` is the confirmed renderer/math leaf `CopyTransformMatrix`. It
copies the meaningful fields of a PSX `MATRIX`-layout transform from `a0` to
`a1`: nine signed/Q12 rotation halfwords followed by three signed 32-bit
translation components. It performs no matrix arithmetic.

Its recovered ABI is:

```c
void CopyTransformMatrix(const MATRIX *source, MATRIX *destination);
```

## Exact boundary and ownership

The preceding owner returns at `0x80135CD4`; its delay-slot store at
`0x80135CD8` is the predecessor's final mutation. `CopyTransformMatrix`
begins independently at `0x80135CDC`, is frameless, saves no registers, and
makes no calls. It returns at `0x80135D48` with a NOP delay slot at
`0x80135D4C`. The next independent leaf begins at `0x80135D50`.

The exact interval is therefore `[0x80135CDC,0x80135D50)`, or `0x74` bytes,
matching the generated range. Generated labels at `0x80135CEC`,
`0x80135CFC`, `0x80135D2C`, and `0x80135D48` are loop/control continuations,
not ABI entries. An exhaustive aligned scan finds no JAL to an interior
address.

## ABI and copied layout

`a0` is the source pointer and `a1` is the destination pointer. No pointer is
nullable in the implementation: a null or invalid pointer faults on the first
load/store. The routine returns without intentionally setting `v0`; callers
do not consume a scalar result.

The nested loops copy these fields:

| Source offsets | Destination offsets | Width | Meaning |
|---|---|---:|---|
| `+0x00,+0x02,...,+0x10` | same | 16 bits each | nine rotation matrix elements |
| `+0x14` | `+0x14` | 32 bits | translation X |
| `+0x18` | `+0x18` | 32 bits | translation Y |
| `+0x1C` | `+0x1C` | 32 bits | translation Z |

Rotation loads use `lhu`, then stores use `sh`. This preserves every 16-bit
bit pattern exactly; signedness and Q12 interpretation matter to consumers
but not to the copy. Translation uses aligned `lw`/`sw`, preserving all 32
bits.

The matrix loop's physical visitation order is:

```text
0, 6, 12, 2, 8, 14, 4, 10, 16
```

That order covers every even offset from `+0x00` through `+0x10` exactly
once. Source and destination use the identical offset, so the routine does
not transpose the matrix despite its column-oriented loop traversal.

## Deliberately uncopied state

The halfword at `+0x12` is padding between the 18-byte rotation array and the
aligned translation vector. It is never loaded or stored. Consequently this
is a field copy, not a raw `memcpy(..., 0x20)`.

No bytes beyond `+0x1F` are touched. In particular, when a `MATRIX` is the
prefix of the game's larger transform object, the signed-halfword state field
at `+0x40` and link/context fields after the matrix remain exactly as they
were. Unlike `SetTransformTranslation` and the two recovered Euler builders,
this routine does not set `+0x40` to one.

The exact-source-equals-destination case is harmless. Arbitrary partial
overlap is not given `memmove` semantics because fields are visited in the
nonlinear order above; no inspected caller relies on partial overlap.

## Call census

The retained executable contains 15 distinct direct guest call PCs:

```text
800E6E88 800E6F20 800E6F34 800E6F48 800E713C
800E7148 800E7154 800E7168 800E8544 800E8840
801263C4 801263D4 80130DD8 80130E68 80130F5C
```

The generated corpus contains 19 call instances because overlay translations
contain a second context for each of `0x800E6E88`, `0x800E6F20`,
`0x800E6F34`, and `0x800E6F48`. Thus the requested 19-caller count describes
generated owner/context instances, while the architectural call-site census
has 15 unique addresses.

Callers pass transform pointers reached through object field `+0x20` or
embedded `0x20`-byte matrices at record offsets such as `+0x08`, `+0x28`,
and `+0x48`. Several sequences invoke this leaf three times at those exact
`0x20` strides, independently corroborating both the matrix identity and its
32-byte containing layout.

## Behavioral equivalent

```c
void CopyTransformMatrix(const MATRIX *source, MATRIX *destination) {
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 3; ++column)
            destination->m[column][row] = source->m[column][row];

    destination->t[0] = source->t[0];
    destination->t[1] = source->t[1];
    destination->t[2] = source->t[2];
}
```

The subscript order above reflects the executable's traversal only; all nine
elements are copied to the same physical offsets, so either conventional
row/column field naming yields the same final structure.

## Evidence

- `disc/SCES_028.45`: exact instruction bytes and 15-PC direct/interior scan;
- `generated/SCES_028.45_full.ranges`: exact `0x74` owner range;
- `generated/SCES_028.45_full_39.c`: complete nested loops and return;
- generated callers: 19 context instances and repeated 32-byte matrix stride.
