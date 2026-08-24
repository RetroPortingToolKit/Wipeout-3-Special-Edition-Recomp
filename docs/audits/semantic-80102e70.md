# Semantic audit: `0x80102E70`

## Result

`0x80102E70` is the confirmed packed-input decoder
`DecodePackedRacerControls`. It splits a 16-bit control sample into two
representations:

1. low packed bits `0..9` are expanded into the sparse runtime action word at
   racer record offset `+0x0C`;
2. packed bits `10..15` are returned as an unsigned six-bit auxiliary value.

Its recovered ABI is:

```c
uint32_t DecodePackedRacerControls(int32_t racer_index,
                                  uint32_t packed_controls);
```

Only the low 16 bits of `packed_controls` participate.

## Exact owner and callable proof

The preceding owner ends with `jr ra` at `0x80102E68` and forms its packed
return in the delay slot at `0x80102E6C`. `DecodePackedRacerControls` begins
independently at `0x80102E70`, is frameless, saves no registers, and makes no
calls. It returns at `0x80102FB0`, with the upper-bit extraction in its delay
slot at `0x80102FB4`. The next owner opens a 40-byte frame at `0x80102FB8`.

The exact range is therefore `[0x80102E70,0x80102FB8)`, or `0x148` bytes,
matching the generated owner table. Generated labels from `0x80102EAC`
through `0x80102FB0` are internal per-bit branches retaining live `a0/a1`
and the derived destination pointer. An exhaustive aligned JAL scan finds no
call to any interior address.

The entry has exactly seven direct callers:

```text
8010306C 801030D4 801031BC 8010322C 80114A64 80114B44 80114B54
```

This direct caller evidence, independent return, and lack of required
predecessor state prove that the ranked alternate is a real ABI function.

## Racer-record address and mutation

The destination is derived with exact multiply-by-`0x3A4` shift/add
arithmetic:

```text
record = 0x801910E0 + racer_index * 0x3A4
action_word = (uint32_t *)(record + 0x0C)
```

The `0x801910E0` base and `0x3A4` stride match independently audited
racer/controller records throughout the session and view code. There is no
negative-index check, upper-bound check, null check, or alignment check;
callers are responsible for supplying an in-range index.

The function always clears `*action_word` before testing any packed bit. It
then repeatedly loads, ORs, and stores that same word for each set bit. Thus
the final action word depends only on this sample's low ten bits; prior action
state is replaced rather than accumulated across calls.

No other record field or global is mutated.

## Exact packed-to-runtime bit mapping

After truncation to `uint16_t`, the low bits map as follows:

| Packed input bit | Packed mask | Runtime action bit | Runtime mask |
|---:|---:|---:|---:|
| 9 | `0x0200` | 15 | `0x00008000` |
| 8 | `0x0100` | 14 | `0x00004000` |
| 7 | `0x0080` | 13 | `0x00002000` |
| 6 | `0x0040` | 11 | `0x00000800` |
| 5 | `0x0020` | 10 | `0x00000400` |
| 4 | `0x0010` | 9 | `0x00000200` |
| 3 | `0x0008` | 7 | `0x00000080` |
| 2 | `0x0004` | 6 | `0x00000040` |
| 1 | `0x0002` | 5 | `0x00000020` |
| 0 | `0x0001` | 4 | `0x00000010` |

Runtime bits `0..3`, `8`, `12`, and `16..31` remain zero because the word is
cleared first and no input maps to them. The mapping is a sparse expansion,
not an arithmetic shift or PSX pad-bit identity conversion.

The adjacent preceding encoder at `0x80102D9C` performs the inverse mapping
for these ten runtime bits, corroborating both direction and field role. That
predecessor is not merged into this audit because it has its own generated
range and callable boundary.

## Return and caller use

The return is formed exactly as:

```c
return ((uint16_t)packed_controls) >> 10;
```

It is therefore always in `0..63`; no sign extension occurs. The four live
input/replay-update callers at `0x8010306C`, `0x801030D4`, `0x801031BC`, and
`0x8010322C` store the result to the word at `0x80173AF8`. The three stream
decode/application calls at `0x80114A64`, `0x80114B44`, and `0x80114B54`
invoke the same action-word mutation while not retaining the auxiliary scalar.

Some live callers first remap all 16 packed bits through a configurable table;
others read saved halfwords from racer-record offsets `+0x12C` or `+0x218`.
The later callers consume sequential halfwords from a playback/data stream.
Together these uses establish a generic packed-control decoder shared by live,
saved, and replay-style input paths, rather than a device-polling routine.

## State and side-effect limits

The function writes exactly one racer action word, potentially multiple times
while ORing bits. It performs no heap operation, callback, hardware access,
GTE operation, global read/write, or error reporting. It clobbers only normal
volatile integer registers and establishes the six-bit scalar in `v0`.

## Behavioral equivalent

```c
uint32_t DecodePackedRacerControls(int racer, uint32_t input) {
    static const uint8_t output_bits[10] = {
        4, 5, 6, 7, 9, 10, 11, 13, 14, 15
    };
    uint16_t packed = (uint16_t)input;
    uint32_t *actions = (uint32_t *)(0x801910E0 + racer*0x3A4 + 0x0C);
    *actions = 0;
    for (unsigned bit = 0; bit < 10; ++bit)
        if (packed & (1u << bit))
            *actions |= 1u << output_bits[bit];
    return packed >> 10;
}
```

The executable spells the ten cases as straight-line conditional branches,
not a loop or lookup table.

## Evidence

- `disc/SCES_028.45`: authoritative instructions, seven-call census, and zero
  interior JAL targets;
- `generated/SCES_028.45_full.ranges`: exact `0x148` owner range;
- `generated/SCES_028.45_full_23.c`: complete bit mapping and return;
- all seven callers: live/remapped, saved-record, and stream input sources;
- adjacent `0x80102D9C`: independently bounded inverse sparse-bit encoder.
