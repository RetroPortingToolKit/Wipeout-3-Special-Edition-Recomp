# Semantic audit: `0x8014B6C0`

## Result

`0x8014B6C0` is `WrapAngle4096Signed`, an effect-free angle normalization leaf.
It reduces any integer to the title's 4096-unit turn and returns the signed
representative in `[-2047, 2048]`.

## Semantic extent and generated split

The generated unique prefix is `R 8014B6C0 18`, covering
`0x8014B6C0..0x8014B6D7`.  Its nonnegative branch targets `0x8014B6D8`, whose
`jr ra`/NOP pair is separately emitted as `F/R 8014B6D8 8`.  The next real
owner begins at `0x8014B6E0`.

The source-level callable entered at `0x8014B6C0` therefore has semantic extent
`[0x8014B6C0,0x8014B6E0)`.  `0x8014B6D8` has zero direct JAL callers and is
cataloged as the internal nonnegative return block, not a second API.

## ABI and exact formula

The recovered ABI is:

```c
int32_t WrapAngle4096Signed(int32_t angle);
```

The instruction-level formula is:

```c
uint32_t wrapped = (uint32_t)angle & 0xFFF;
return wrapped <= 0x800 ? (int32_t)wrapped : (int32_t)wrapped - 0x1000;
```

Thus inputs reduce modulo 4096.  Values `0..2048` remain nonnegative, while
`2049..4095` become `-2047..-1`.  The exact half-turn representative is
positive `+2048`, not `-2048`.  `a1`--`a3` are unused.  The leaf reads and
writes no memory, calls no helper, and has no hardware or global effects.

## Executable callers and false positives

The retained base EXE contains exactly 17 aligned JAL sites:

```text
800E4668 800E4674 800E4680 800EEFE8 800EFB9C 800EFEBC
800F1DE4 800F1DF0 800F1DFC 8012453C 80136B50 80136DE4
8013724C 8014C868 8014C874 8015A64C 8015A658
```

Grouped base owners are `0x800E39E4` (three), `0x800EEF04`, `0x800EF810`,
`0x800EFCE8`, `0x800F1D5C` (three), `0x801238F0`, `0x801369BC`, confirmed
`PlaySpatialSoundAtSegmentMidpoint` (`0x80136D0C`), `0x80136F58`,
`0x8014C704` (two), and `0x8015A45C` (two).

Generated output contains 26 call instances because the three guest sites at
`0x800E4668/74/80` occur in four mutually exclusive overlay variants.  Counting
unique guest call addresses yields the correct 17-call executable interface.
Unlike the neighboring division audit, the raw scan finds no additional
opcode-shaped data words targeting this entry.  No call targets
`0x8014B6D8`, and no other external J/JAL enters the semantic interior.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for the 17 unique
  JAL sites and exact arithmetic;
- `generated/SCES_028.45_full.ranges` for the adjacent `18`/`8` split;
- generated overlay instances for reconciling 26 emitted calls to 17 unique
  guest addresses.
