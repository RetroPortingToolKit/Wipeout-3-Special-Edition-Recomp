# Semantic audit: `0x8015FB14`

## Result

`0x8015FB14` is the confirmed straight-line MATRIX initializer
`InitializeMatrixRotationFromFourCoefficients`. It installs four caller-supplied
rotation coefficients, clears the remaining five rotation coefficients and the
alignment halfword, preserves the translation vector, and returns the input
MATRIX pointer.

The recovered ABI is:

```c
MATRIX *InitializeMatrixRotationFromFourCoefficients(
    MATRIX *matrix,
    int16_t m00,
    int16_t m01,
    int16_t m02,
    int16_t m10);
```

The fifth argument is read from the o32 incoming argument slot at `sp+0x10`.
All coefficient stores truncate to signed 16-bit MATRIX elements.

## Boundary and control flow

The generated function/range tables both place the owner at `0x8015FB14` with
size `0x3C`. The body is one branch-free, call-free block: it begins by loading
the stack argument, returns through `jr ra` at `0x8015FB48`, and performs the
final zero store in the delay slot at `0x8015FB4C`. Thus the exact executable
interval is `[0x8015FB14,0x8015FB50)`. The preceding watchdog helper ends at
`0x8015FB14`; bytes `0x8015FB50..0x8015FB53` are not part of this owner, and
the next independent generated owner begins at `0x8015FB54`.

There are no branches, interior labels, or calls, and an aligned target scan
finds no jumps or calls into its interior.

## Caller census

Seven generated JAL sites target the true entry, with seven unique addresses
and no overlay duplicates or data-derived false callers:

```text
0x800EDA3C
0x800EDA5C
0x80115D84
0x80115D9C
0x80115FC8
0x80115FE0
0x801161D0
```

Representative call sites pass constants such as `(0, 0x100, 0x1E0, value)`
and `(0, 0, 0x3C0, 0x1E0)`, confirming that the four scalar arguments are
matrix coefficients rather than a source pointer or packed object.

## Data effects

The stores produce this rotation matrix prefix:

```text
[ m00  m01  m02 ]
[ m10    0    0 ]
[   0    0    0 ]
```

More exactly, the function writes halfwords at `+0`, `+2`, `+4`, and `+6`,
zeros halfwords `+8`, `+0xA`, `+0xC`, `+0xE`, and `+0x10`, and clears MATRIX
padding at `+0x12`. It never accesses translation words `+0x14`, `+0x18`, or
`+0x1C`. This complements `SetMatrixTranslation` (`0x80135DD8`) but does not
invoke it and has no global, GPU, GTE, or other hardware side effects.

The symbol is intentionally descriptive rather than assigned to a canonical
Psy-Q API: the evidence proves the exact partial-rotation initializer, but not
a standard SDK identity.
