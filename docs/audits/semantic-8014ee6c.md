# Semantic audit: `0x8014EE6C`

## Result

`0x8014EE6C` is executable and appears in coverage, but it is **not a callable
function entry**. It is a straight-line instruction inside jump-table case 6
of the function at `0x8014EA88`, named `SpawnRacerEffectRecords` in the symbol
catalog. The generated `func_8014EE6C` is an AOT split-piece artifact.

The exact semantic owner is `[0x8014EA88, 0x8014F01C)`, or `0x594` bytes. Its
prologue at `0x8014EA88` allocates `0x28` stack bytes and saves `ra/s0/s1/s2`;
its sole epilogue restores them at `0x8014F004..0x8014F018`. The next function
starts at `0x8014F01C` with a new `0x50`-byte stack frame.

## Behavior

The owner accepts four arguments:

- `a0`: racer/effect owner; byte `a0+0x15F` is cleared by most spawn cases;
- `a1`: base of a 30-entry effect-record array;
- `a2`: racer/value argument stored or used to derive a lifetime;
- `a3`: effect kind, truncated to a byte and dispatched for values 1..15.

It scans at most 30 records of `0x60` bytes. A zero halfword at record offset
`+0x00` denotes a free slot. The selected jump-table case initializes fields
including lifetime `+0x00`, racer index `+0x53`, kind `+0x54`, callback `+0x58`,
and state `+0x5C`. Several kinds call `0x8013491C` or `0x80134964` and may also
invoke `0x80137DFC`. Invalid kinds and a full pool leave without a spawn.

Kind 6 enters at `0x8014EE54`. It computes signed lifetime `a2+12`, locates the
free record, and at the audited `0x8014EE6C` begins installing callback
`0x80151A84`. It then writes kind 6, clears state, stores the racer index and
lifetime, and either continues scanning when the lifetime is below 60 or exits.

## Call and alias proof

There are exactly three direct `jal 0x8014EA88` instructions in the main EXE,
at `0x8015B3F8`, `0x8015B40C`, and `0x8015B424`. All belong to the wrapper at
`0x8015B3CC`; it maps incoming kind 13 to two owner calls with kinds 14 and 15,
and otherwise forwards the original kind once. No `jal` targets `0x8014EE6C`.

The owner's 15 jump-table entries at `0x8016EA64` are, in kind order:

`8014EC30, 8014ED94, 8014EB38, 8014EB98, 8014ECE4, 8014EE54,
8014EF98, 8014EC84, 8014EB00, 8014EBD8, 8014EDF4, 8014ED44,
8014EE9C, 8014EEE0, 8014EF24`.

Those addresses are internal case aliases, not independent functions. The AOT
generator additionally exposes `0x8014EE9C`, `0x8014EEE0`, `0x8014EF24`, and
`0x8014EF98` through the overlapping alias body attached to generated
`func_8014EE6C`; that is host code partitioning, not guest ABI evidence.

## Evidence

- `disc/SCES_028.45`, load address `0x80010000`, direct little-endian MIPS
  disassembly and exact opcode scan;
- `generated/SCES_028.45_full_128.c`, split-piece and overlapping-alias output;
- `generated/SCES_028.45_full_132.c`, the three caller continuations;
- `generated/SCES_028.45_full.ranges`, overlapping generated ranges.
