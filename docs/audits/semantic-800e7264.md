# Semantic audit: `0x800E7264` / `DecodeAnimationCommandRecord`

## Result

`0x800E7264` decodes one packed animation command record:

```c
uint8_t DecodeAnimationCommandRecord(const uint8_t *stream,
                                     int16_t byte_offset,
                                     uint32_t arguments[4]);
```

It returns the opcode byte at `stream[byte_offset]` and reconstructs the next
16 bytes as four unaligned little-endian argument words. The surrounding
callers use that opcode to advance and dispatch animation tracks.

## Exact logical owner and generated split

The logical callable is `[0x800E7264,0x800E735C)`, exactly `0xF8` bytes. The
range generator incorrectly emitted two consecutive functions:

```text
F 800E7264 / R 800E7264 C0
F 800E7324 / R 800E7324 38
```

There is no control-flow boundary at `0x800E7324`: execution falls directly
from four byte loads at `0x800E7314..0x800E7320` into a temporary 16-byte stack
allocation, assembles/stores the fourth word, and returns at `0x800E7354` with
stack restoration in the delay slot. `0x800E735C` begins a separate framed
routine with independent global initialization and a call.

An exhaustive aligned jump/JAL scan finds no target inside the owner. A byte
scan of the authenticated executable finds no second copy of the full body;
thus overlay/range overlap does not create another logical implementation.
The split at `0x800E7324` is the only ownership correction required.

## ABI and exact behavior

The first two instructions explicitly truncate and sign-extend `a1` to 16
bits. The source accesses are byte loads, so the stream and record need not be
aligned. In exact terms:

```c
const uint8_t *record = stream + (int16_t)byte_offset;
uint8_t opcode = record[0];
for (unsigned i = 0; i != 4; ++i) {
    const uint8_t *p = record + 1 + 4*i;
    arguments[i] = (uint32_t)p[0]
                 | (uint32_t)p[1] << 8
                 | (uint32_t)p[2] << 16
                 | (uint32_t)p[3] << 24;
}
return opcode;
```

The four results are ordinary word stores at output offsets `0`, `4`, `8`,
and `12`. The routine performs no validation or bounds checking; callers must
supply a valid 17-byte record and writable, word-aligned output. It calls no
subroutine and changes no global, hardware, callback, or interrupt state.

## Complete caller census and role

There are exactly six direct calls, all targeting the true entry:

```text
800E562C 800E5864
800E74D8 800E7640 800E7868 800E78AC
```

The first two walk command streams with a signed 16-bit offset, interpret the
returned opcode through animation-specific tables and cases, and advance the
offset. The four sites in the large owner beginning at `0x800E7428` repeatedly
decode the current record into a 16-byte stack buffer; the first dispatches
opcodes 1 through 13 through a jump table, while later cases require exact
opcodes 1, 6, or another saved selector before updating animation timing and
track state. Every caller consumes both the opcode return and decoded words.

The nearby retained data map independently identifies the handler tables
covering this code region as animation primitive/chunk handlers. Combined
with the track-offset iteration and opcode dispatch, this supports
`DecodeAnimationCommandRecord`; the name is descriptive and does not claim an
original source identifier.

## Sources checked

- `disc/SCES_028.45`, excluding its `0x800` PS-X EXE header, for exact bytes,
  disassembly, duplicate-body search, and exhaustive direct/interior targets;
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_full_15.c` for the false split and complete flow;
- `docs/audits/data-map-8016cc0c.md` for the independently recovered animation
  dispatch-table ownership of the surrounding code.
