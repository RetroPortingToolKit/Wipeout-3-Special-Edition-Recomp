# Semantic audit: `0x80118FDC`

## Result

`0x80118FDC` is the confirmed mutable-vector helper
`NormalizeVector3Q12InPlace`. It estimates a three-component vector's length
at reduced scale and overwrites the vector with an approximately unit-length
Q12 direction.

Its recovered ABI is:

```c
void NormalizeVector3Q12InPlace(int32_t vector_xyz[3]);
```

The vector contains three consecutive signed 32-bit components at offsets
`+0`, `+4`, and `+8`. No other input arguments are consumed.

## Exact boundary and call graph

The preceding owner restores its frame and returns at
`0x80118FCC..0x80118FD8`. This owner opens a fresh 24-byte frame at
`0x80118FDC`, saves `s0` and `ra`, and has one epilogue. It returns at
`0x80119130` with stack restoration in the delay slot at `0x80119134`. The
next owner opens a separate 72-byte frame at `0x80119138`.

Its exact interval is therefore `[0x80118FDC,0x80119138)`, or `0x15C` bytes,
matching the generated range. The many generated entries from `0x80119034`
through `0x80119124` are post-square-root and compiler-generated signed
division branches retaining this owner's frame. An exhaustive aligned scan
finds no call to any interior address.

There are exactly nine direct callers:

```text
800EFAC8 800F3A40 800F3B88 800F6748 800F6750
800F7330 800F73F8 800F92CC 800F93A8
```

All nine pass mutable three-word work vectors, usually just constructed from
differences between positions or vertices. Callers reload the normalized
components from that storage after the call. None consumes `v0` as a scalar
result, and no call targets an interior continuation.

## Exact arithmetic

Let the original signed components be `x`, `y`, and `z`. The routine first
computes, using 32-bit MIPS low-product/add behavior:

```text
sx = x >> 6
sy = y >> 6
sz = z >> 6
squared_sum = sx*sx + sy*sy + sz*sz
reduced_length = ApproximateSqrtNonnegative32(squared_sum)
denominator = reduced_length != 0 ? reduced_length : 1
```

The right shifts are arithmetic. Reducing each component before squaring
extends the useful authored geometry range and makes `reduced_length`
approximately `length(original)/64`.

It then overwrites the input words in X/Y/Z order:

```text
vector[0] = (x << 6) / denominator
vector[1] = (y << 6) / denominator
vector[2] = (z << 6) / denominator
```

Signed MIPS division truncates toward zero. Combining the scale factors gives
approximately:

```text
output ~= original * 4096 / length(original)
```

so `0x1000` represents magnitude 1.0. The output is an approximate Q12 unit
vector, not a preserved-coordinate vector or a returned length.

`ApproximateSqrtNonnegative32` truncates through a Q12 mantissa table and may
underestimate the exact reduced length. Output magnitude can consequently be
slightly above `0x1000`; this owner performs no post-normalization correction
or saturation.

## Zero, quantization, and overflow behavior

For the exact zero vector, the squared sum and approximate length are zero,
the denominator is replaced with one, and all three stored results remain
zero. The same denominator substitution also applies when every component's
arithmetic `>>6` reduction is zero. Thus very small vectors are not normalized
to unit length; for example `(1,0,0)` becomes `(64,0,0)`, whereas
`(64,0,0)` becomes `(4096,0,0)`.

The three compiler-emitted signed-division guard sequences include divide-by-
zero traps, but the explicit substitution makes those paths unreachable.
The `INT_MIN / -1` overflow guards are also outside ordinary nonnegative-
length use.

All multiplies, additions, and left shifts retain only 32-bit MIPS results.
Very large components can overflow the reduced square sum or `(component<<6)`;
if the sum acquires bit 31 it also leaves the documented nonnegative domain of
the approximate-square-root helper. There is no overflow detection, clamp, or
fallback. The confirmed semantics apply directly to the bounded geometry
vectors supplied by the nine callers.

## Mutation, state, and return

The only RAM mutation is the three word stores through the input pointer. The
pointer is not checked for null or alignment. In-place operation is mandatory
to the observed callers; the original components are loaded before their
corresponding overwrite and the shared denominator is computed before any
store.

The sole callee is `ApproximateSqrtNonnegative32` at `0x8015C488`. That helper
writes GTE `LZCS` and reads `LZCR`, so this owner transitively clobbers that GTE
leading-sign-count state but does not touch GTE matrices, vectors, MACs, GPU
state, globals, or heap state.

After storing normalized Z, `v0` still happens to contain that final quotient.
This is not a stable public return contract: the function does not explicitly
form a result, X/Y-only interpretations would be misleading, and every caller
ignores `v0`. The ABI is therefore modeled as `void` with in-place output.

## Behavioral equivalent

```c
void NormalizeVector3Q12InPlace(int32_t v[3]) {
    int32_t x = v[0], y = v[1], z = v[2];
    int32_t sx = x >> 6, sy = y >> 6, sz = z >> 6;
    int32_t d = ApproximateSqrtNonnegative32(
        sx*sx + sy*sy + sz*sz);
    if (d == 0)
        d = 1;
    v[0] = (x << 6) / d;
    v[1] = (y << 6) / d;
    v[2] = (z << 6) / d;
}
```

The pseudocode assumes wraparound operations matching the MIPS implementation
rather than ISO C signed-overflow rules.

## Evidence

- `disc/SCES_028.45`: authoritative disassembly, exact caller census, and
  zero interior targets;
- `generated/SCES_028.45_full.ranges`: exact `0x15C` owner range;
- `generated/SCES_028.45_full_30.c`: complete square, call, division, stores,
  and epilogue;
- `docs/audits/semantic-8015c488.md`: independently recovered approximate
  square-root domain and truncation behavior;
- the nine callers' position/vertex-difference work-vector construction.
