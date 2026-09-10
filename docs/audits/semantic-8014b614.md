# Semantic audit: `0x8014B614` / `0x8014B638`

## Result

The true callable owner begins at `0x8014B614` and is named
`ApproximateVectorLength3i`. The generated `0x8014B638` function is an internal
fallthrough created where the compiler delays stack-frame setup until just
before the only call. The recovered operation is:

```c
uint32_t ApproximateVectorLength3i(int32_t x, int32_t y, int32_t z)
{
    uint32_t squared =
        (uint32_t)(x * (int64_t)x) +
        (uint32_t)(y * (int64_t)y) +
        (uint32_t)(z * (int64_t)z);
    return SqrtApproxU31(squared);  /* 0x8015C488 */
}
```

The arithmetic is an integer/fixed-point magnitude operation: there is no
scale shift between squaring and square root, so the result retains the input
unit. The explicit modulo-`2^32` wording matters because each MIPS `mult`
contributes only `LO`, and the two additions are ordinary 32-bit `addu`.
Callers are expected to keep the squared sum in the callee's nonnegative
domain; the routine does not detect overflow.

## Exact owner boundary

- `0x8014B614..0x8014B634` computes `x*x`, `y*y`, and begins `z*z`. There is no
  return or transfer before `0x8014B638`.
- `0x8014B638` allocates 24 bytes and saves `ra`. The stores of `a0..a3` above
  the frame are ABI argument-home saves; only the already latched products feed
  the result, and `a3` has no semantic input role.
- `0x8014B650..0x8014B65C` adds the three `LO` values and passes the sum in
  `a0` to `0x8015C488`.
- `0x8014B660..0x8014B66C` restores `ra` and `sp`, then returns the square-root
  result unchanged in `v0`.
- The exact owner is therefore `0x8014B614..0x8014B66F`, `0x5C` bytes. The
  independent routine at `0x8014B670` starts by testing the sign of its own
  `a0` and implements unrelated absolute-value behavior.

The apparent prologue at `0x8014B638` cannot be called independently: it
requires `v0=x*x`, `v1=y*y`, and the `z*z` product still live in `LO` from the
straight-line prefix. An ABI call there would supply none of those values.

## Square-root callee proof

`0x8015C488` is a table-normalized integer square-root approximation, not a
floating-point library call:

1. It writes the nonnegative input to GTE `LZCS` and reads `LZCR`, obtaining
   its leading-zero count. A count of 32 returns zero through `0x8015C504`.
2. It rounds the leading-zero count down to an even value, derives a half-size
   exponent, and shifts the input into a 64..255 mantissa interval.
3. It indexes the 192 signed halfwords at `0x8017AF5C`. Those coefficients run
   from 4096 through 8175 and approximate the square root of that normalized
   mantissa in Q12.
4. It rescales the coefficient by the derived exponent, shifts right by 12,
   and returns the integer approximation.

Representative results distinguish its semantics and approximation from an
exact `isqrt`: `sqrt(1)=1`, `sqrt(4)=2`, `sqrt(4096)=64`, while input
`1,000,000` returns `999` rather than exact floor root `1000`. Thus the owner
name retains `Approximate` rather than overclaiming exact Euclidean length.

## Complete direct callers

An aligned scan of every instruction word in the retained PS-X EXE finds 38
JALs to the true entry:

```text
800E39F4 800E42C8 800E45BC 800E90A0 800E9810 800EA2F8 800EFC68
800F05E4 800F17A4 800F2314 800F232C 800F2344 800F679C 800F67B4
800F9BE4 8010E5C4 8010E5F0 801192DC 8011960C 80119638 80136B14
80136D90 80137148 801371EC 8014ADC0 8014ADD8 8014ADF0 8014AE08
8014B148 8014B160 8014B178 8014B440 8014B458 80151D6C 80154334
80155910 801592A0 8015AB14
```

There are zero JALs to `0x8014B638`. The call at `0x80136D90`, independently
audited in `semantic-80136d0c.md`, passes XYZ coordinate differences and uses
the result as a spatial distance for sound attenuation. Other call clusters
likewise pass three signed components, supporting the general vector-length
role rather than a subsystem-specific name.

The square-root helper has 33 direct calls overall, including the owner's sole
call at `0x8014B658`; it is shared rather than private to this vector wrapper.

## Catalog decision

- `0x8014B614`: confirmed function `ApproximateVectorLength3i`, `emit=false`.
- `0x8014B638`: confirmed internal block `ApproximateVectorLength3iCallFrame`.

## Sources checked

- `disc/SCES_028.45`: authoritative instructions and exhaustive JAL scans.
- `generated/SCES_028.45_full_127.c`: owner prefix, fallthrough split, call,
  epilogue, and adjacent next routine.
- `generated/SCES_028.45_full_132.c`: complete square-root approximation and
  zero-input continuation.
- `generated/SCES_028.45_full.ranges`: generated `0x24`/`0x38` false split.
- `docs/audits/semantic-80136d0c.md`: independent spatial-distance consumer.
