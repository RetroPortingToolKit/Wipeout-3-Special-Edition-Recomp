# Semantic audit: `0x8015B5D8` / `NextRacerRandom15`

## Result

`0x8015B5D8` is a second independent instance of the game's 15-bit XOR-shift
generator, now named `NextRacerRandom15`:

```c
uint32_t NextRacerRandom15(void);
```

It advances private state `gRacerRandom15State` at `0x80173A98` and returns a
value in `0..32767`. Its callers use this stream in racer/gameplay state paths.

## Exact owner and ABI

Generated metadata assigns exactly `0x24` bytes to `0x8015B5D8`. The leaf is
frameless, argument-free, and call-free. It returns at `0x8015B5F4`, masks the
result in the delay slot at `0x8015B5F8`, and the next independent owner begins
at `0x8015B5FC`. The exact callable is `[0x8015B5D8,0x8015B5FC)`.

No caller supplies a meaningful argument. `v0` returns the new state's low 15
bits. The only persistent mutation is the full-word state store.

## Exact recurrence and independent stream

The implementation is:

```c
uint32_t NextRacerRandom15(void) {
    int32_t old = (int32_t)gRacerRandom15State;
    uint32_t next = (uint32_t)(old >> 1) ^ ((uint32_t)old << 3);
    gRacerRandom15State = next;
    return next & 0x7FFF;
}
```

The right shift is explicitly arithmetic (`sra`), while the left shift and XOR
operate modulo 32 bits. The full unmasked result is stored before only the
returned value is masked.

The PS-X EXE initializes `0x80173A98` to `0x12345678`. The adjacent established
`NextRandom15` at `0x80104AA0` implements the same recurrence but advances the
separate word `gRandom15State` at `0x80173A94`. Neither leaf reads or writes the
other stream. Keeping separate symbols is therefore required for deterministic
state reconstruction.

## Callers and overlay duplication

An exhaustive aligned scan of the retained executable finds exactly three
unique JAL instructions:

```text
801546A4  80154924  80156FA0
```

All target the entry; no J/JAL targets an interior instruction. The generated
corpus emits six calls because each of these three instruction words appears in
two overlapping translations. There are three semantic call sites, not six.

The first two callers divide the returned value by gameplay-selected positive
ranges and consume the remainder. The third applies constant reciprocal
arithmetic to reduce the random result before updating a selected `0x3A4`-byte
racer/session record. Another caller gate directly examines runtime-racer byte
`+0x15F`. These consumers establish random range selection in racer/gameplay
logic rather than an opaque counter or status code.

## Defensible naming

`NextRacerRandom15` distinguishes this low-fanout gameplay stream from the
high-fanout general `NextRandom15` stream while retaining the exact observable
15-bit output width. The name does not imply the ANSI/PsyQ `rand` linear
congruential algorithm, which this recurrence is not.

## Sources checked

- `disc/SCES_028.45`, accounting for its `0x800`-byte PS-X EXE header, for
  exact instructions, initial state, unique callers, and target scans;
- `generated/SCES_028.45_full.ranges` for the exact owner boundary;
- generated caller emissions for the six-emitted/three-unique overlay proof;
- `docs/audits/semantic-80104aa0.md` for the independently proven adjacent
  PRNG stream and recurrence comparison.
