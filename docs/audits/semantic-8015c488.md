# Semantic audit: `0x8015C488`

## Result

`0x8015C488` is the confirmed scalar helper
`ApproximateSqrtNonnegative32`. It uses the GTE leading-sign-count registers to
normalize a nonnegative 32-bit integer, looks up a Q12 square-root mantissa,
rescales it by half the binary exponent, and returns an integer approximation
to `sqrt(input)` in the same unit as the square root of the input.

Its recovered ABI is:

```c
uint32_t ApproximateSqrtNonnegative32(uint32_t input);
```

The supported domain is `0..INT32_MAX`. Values with bit 31 set are interpreted
by GTE leading-sign normalization rather than as ordinary unsigned magnitudes;
the routine has no domain check and callers are responsible for keeping their
squared values nonnegative.

## Logical boundary and false split

The nonzero entry begins at `0x8015C488` and is a frameless leaf. At
`0x8015C49C`, zero input's GTE count of 32 branches to `0x8015C504`, whose
`jr ra` and `li v0,0` delay slot are the function's zero-return tail. Nonzero
inputs return at `0x8015C4FC..0x8015C500`. The internal zero tail ends at
`0x8015C508`; three alignment NOPs occupy `0x8015C50C..0x8015C514`, and the
next independent callable begins at `0x8015C518`.

The logical owner is therefore `[0x8015C488,0x8015C50C)`, or `0x84` bytes.
The generated manifest instead records `R 8015C488 7C` plus
`F/R 8015C504 8`; the latter is an internal tail, not an ABI function. An exact
JAL scan finds zero direct calls to `0x8015C504`.

## GTE normalization

The first instruction writes input to GTE `LZCS`; after two required NOPs the
routine reads `LZCR`, the count of leading sign bits. For supported nonnegative
input this is the ordinary leading-zero count `lz`. A value of 32 identifies
zero and returns zero.

For nonzero input it computes:

```text
e = lz & ~1                    // round leading-zero count down to even
shift = e - 24
mantissa = shift >= 0 ? input << shift : input >> -shift
```

The resulting mantissa is always in `64..255`. If `lz` is even its top bit is
at bit 7; if `lz` is odd its top bit is at bit 6. Rounding `lz` down to even is
what makes the later exponent division exact without a fractional power of
two.

The scale exponent is:

```text
half_exponent = (31 - e) >> 1
```

## Lookup table and output scaling

The helper indexes signed halfword
`kApproximateSqrtMantissaQ12[mantissa - 64]` at `0x8017AF5C`. The table has
exactly 192 entries, one for every normalized value 64 through 255, and each
coefficient is:

```text
table[n - 64] = floor(512 * sqrt(n))
              = floor(4096 * sqrt(n / 64))
```

Thus it is a Q12 square root of the mantissa normalized by 64. The retained
table is monotone from 4096 to 8175. Its exact 384-byte SHA-256 is
`45ca1b7b619b33961c03c82243b8baf59fc50c49b4d7eefc94b76edbd417a5c8`.

The returned value is computed exactly as:

```text
result = (table[mantissa - 64] << half_exponent) >> 12
```

The final shift is logical, but all supported-domain intermediates are
nonnegative. Algebraically this rescales the normalized coefficient back into
the input's integer domain, so an input formed as `x*x+y*y+z*z` returns a
length in the same unit as `x`, `y`, and `z`; there is no hidden Q-format on
the public result.

The table and final integer shift truncate downward. Representative outputs
are `sqrt(0)->0`, `sqrt(1)->1`, `sqrt(4)->2`, `sqrt(4096)->64`, and
`sqrt(1,000,000)->999` rather than the exact floor root 1000.

## Caller census and use

An aligned opcode scan finds exactly 33 direct calls:

```text
800E3F5C 800E96DC 800E9B28 800E9E8C 800EA044 800EA1C4 800EA4E8
800F32A4 800F3540 800F8D18 8011902C 80124E74 801250C4 801251DC
80125464 8012557C 80126D98 8012C37C 8012C61C 8012C660 8012C6A4
8012C6E8 8013FFE8 8014B658 8014C778 8014D55C 8014D6B4 8014D858
8014DA84 801555D0 801556C4 8015579C 8015A538
```

The call at `0x8014B658` is the scalar core of the independently audited
`ApproximateVectorLength3i`, which passes the low-32-bit sum of three component
squares and returns this result unchanged. Other callers pass analogous
squared scalar quantities. No caller uses the helper as a floating-point or
Q12-return function.

## State and edge behavior

The routine writes only GTE `LZCS` as a normalization input and reads `LZCR`;
it does not alter RAM, global state, the GTE matrix/vector registers, or the
lookup table. It has no stack frame. Zero is explicitly handled. Overflow in
upstream square sums can set bit 31 and leave the supported domain; neither
this helper nor `ApproximateVectorLength3i` detects that condition.

## Evidence

- `disc/SCES_028.45`, exact MIPS/GTE disassembly, table extraction, and JAL
  scans;
- `generated/SCES_028.45_full_50.c`, both generated body fragments;
- `generated/SCES_028.45_full.ranges`, the false tail split;
- `docs/audits/semantic-8014b614.md`, independently recovered vector-length
  owner, domain, and unit semantics.
