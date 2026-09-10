# Semantic audit: `0x8014B670`

## Result

`0x8014B670` is `ApproximateArccosQ12`, a table-driven fixed-point arccosine
leaf.  Input is a signed Q12 cosine and output uses angle units where pi is
`0x4000`.

## Boundary and ABI

The function is frameless and unsplit.  It begins with a sign test at
`0x8014B670`, has one `jr ra` at `0x8014B69C`, and moves the selected result to
`v0` in the delay slot at `0x8014B6A0`.  The next independent owner begins at
`0x8014B6A4`, proving exact extent `[0x8014B670,0x8014B6A4)`, `0x34` bytes.

The recovered ABI is:

```c
int32_t ApproximateArccosQ12(int32_t cosine_q12);
```

`a1`--`a3` are unused.  There are no calls, stack accesses, or stores.

## Exact table formula

For nonnegative input the routine returns signed halfword
`table[cosine_q12]`.  For negative input it indexes by two's-complement
`-cosine_q12` and returns `0x4000 - table[index]`:

```c
unsigned i = cosine_q12 < 0 ? -cosine_q12 : cosine_q12;
int angle = kApproximateArccosQ12AngleTable[i];
return cosine_q12 < 0 ? 0x4000 - angle : angle;
```

The table at `0x8017634C` contains 4097 signed halfwords for indices
`0..4096`.  Samples establish its exact interpretation:

| Q12 index | stored | `floor(acos(i/4096)*16384/pi)` |
|---:|---:|---:|
| 0 | 8191 | 8192 |
| 1024 | 6874 | 6874 |
| 2048 | 5461 | 5461 |
| 3072 | 3769 | 3769 |
| 4095 | 115 | 115 |
| 4096 | 0 | 0 |

Thus the retained table deliberately approximates arccos, with only its zero
entry one unit below the mathematical floor shown.  Reflection implements
`acos(-x) = pi - acos(x)`, producing the full output domain `0..0x4000`.

There is no bounds check.  Authored callers must provide Q12 cosine in
`[-4096,4096]`; larger magnitudes read beyond the table, and `INT32_MIN` also
cannot form an in-range absolute index.  The routine has no global or hardware
effect beyond reading the constant table.

## Exhaustive callers

The retained image and generated control flow agree on exactly nine direct JAL
sites:

```text
800F23D8 8014AEC4 8014AF80 8014B03C 8014B0F8
8014B234 8014B2F0 8014B3AC 8014B4DC
```

They group as one call in owner `0x800F2278`, four in `0x8014AD98`, three in
`0x8014B124`, and one in `0x8014B3EC`.  There are no overlay-duplicate call
instances and no opcode-shaped data false positives.  No external J or JAL
targets an interior instruction.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for instructions,
  the complete 4097-entry table, and caller scan;
- `generated/SCES_028.45_full.ranges` for the exact boundary;
- generated call owners for grouping all nine callers.
