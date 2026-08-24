# Semantic audit: `0x8015FBDC`

## Result

`0x8015FBDC` is the canonical Psy-Q `VectorNormal` wrapper. It loads a
three-word VECTOR, invokes the adjacent GTE-backed Q12 normalization core, and
stores the three normalized words to a caller-provided destination.

Its recovered ABI is:

```c
void VectorNormal(const VECTOR *source, VECTOR *destination);
```

The wrapper has no semantic return value. Source and destination may alias:
all three input words are loaded before the normalization call or any output
store.

## Exact boundary and control flow

The generated function and range tables agree on `[0x8015FBDC,0x8015FC0C)`,
exactly `0x30` bytes. The entry block loads signed 32-bit X/Y/Z from `a0+0`,
`+4`, and `+8` into the private core's `t0/t1/t2` register ABI, saves the
caller's return address in `a3`, and calls `0x8015FC3C`. The continuation at
`0x8015FBF4` stores the resulting `t0/t1/t2` to `a1+0/+4/+8`, restores `ra`
from `a3`, and returns at `0x8015FC04` with a nop delay slot.

`0x8015FBF4` is therefore an internal post-call continuation, not an
independent callable. Its only reference is the wrapper's own JAL link; there
are no external calls or jumps into the interior. The next independent owner
starts exactly at `0x8015FC0C`.

## Caller census

Nine generated call instances collapse to six unique executable JAL sites;
three duplicated sites are overlay copies rather than additional callers:

```text
0x800F272C
0x800FB344
0x800FB5D0
0x800FB6D4
0x800FB794
0x8013F53C
```

Every call targets the true entry at `0x8015FBDC`. Call sites consistently
provide separate or identical three-word source/destination vector addresses,
matching the standard two-pointer VECTOR normalizer contract.

## Behavior and effects

The private core at `0x8015FC3C` squares the three components with GTE `SQR`,
sums them, derives a normalization scale with GTE leading-zero and lookup-table
logic, then uses GTE `GPF` to produce Q12 components in `t0/t1/t2`. The wrapper
exposes that result as three signed 32-bit words. In ordinary nonzero cases the
result is the input direction scaled to a length of approximately `0x1000`.

The only ordinary memory reads are the three source words and the only writes
are the three destination words. The wrapper changes no globals and performs
no GPU, DMA, CD, or BIOS operation. Its GTE effects are scratch state inherited
from the private normalization core; callers consume the stored VECTOR, not
the residual GTE registers.
