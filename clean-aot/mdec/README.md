# Clean-room MDEC device

This directory is a standalone C++20 model of the PlayStation Macroblock
Decoder (MDEC). It is generic device code: it has no disc, BIOS, title address,
renderer, host-video API, thread, filesystem, interpreter, or runtime compiler
dependency. The fixed-size state makes the steady state allocation-free and
snapshot-safe. Snapshots use a versioned, canonical little-endian wire format;
they do not serialize C++ object padding, native endianness, or invalid `bool`
representations.

The device models the two MDEC registers at the bus boundary:

- `write_command_word` accepts an opcode while idle and packed little-endian
  parameter words while a command is active.
- `read_data_word` exposes the bounded output FIFO.
- `write_control_word` implements reset and the two DMA request enables.
- `dma_write_words` and `dma_read_words` enforce request-gated 32-bit DMA
  boundaries. Byte and halfword DMA are rejected explicitly; they are not
  guessed from host byte-lane behavior.
- `status` exposes the documented empty/full/busy/request, output-format,
  current-block, and remaining-parameter fields.

The input side has the documented 0x20-word device boundary. Parameter words
accepted at MDEC0 are tracked separately from bytes still waiting to be
decoded, so DMA0 deasserts after the declared final word even if MDEC remains
busy behind output backpressure. Status bit 30 reports either insufficient
space for a complete input word or the final-word indication. Writes beyond
the declared parameter count fail without entering the next command stream.
Opcodes 4 through 7 have the documented opcode-0 no-op behavior.

The implementation includes quantization-table and scale-table loads, FE00
padding/EOB and run-overflow block termination, signed 10-bit RLE coefficients,
normal inverse-zig-zag placement, the `q_scale=0` natural-order path,
two-pass integer IDCT, 4/8-bit monochrome output, 15/24-bit color output,
signed and unsigned modes, bit-15 control, 4:2:0 macroblock reconstruction,
bounded FIFOs, fail-closed malformed-stream handling, snapshots, a stable
state digest, and an explicit deterministic device-cycle counter.

The integer output contract is deliberately written down because the public
hardware documentation leaves some silicon details under-specified. Scale
values use the upper 13 bits, each IDCT pass uses the documented matrix indexing
and deterministic `floor((sum + 0x0fff) / 0x2000)` rounding, and YUV conversion uses signed Q12
coefficients (`1.402`, `0.34375`, `0.71436`, and `1.772`). Color bytes are
emitted in BGR order, and 15-bit pixels use PS1 BGR555 bit placement with the
command-controlled bit 15. These choices are deterministic implementation
behavior, not a claim that every undocumented revision has identical rounding.

A color macroblock is decoded in input order `Cr, Cb, Y1, Y2, Y3, Y4` and the
output FIFO exposes the reconstructed pixels as one contiguous row-major
16x16 raster macroblock. The current-block status field carries `Cr=4`,
`Cb=5`, `Y1..Y4=0..3`, and `Y=4` in monochrome mode.

Build and test in isolation:

```text
cmake -S clean-aot/mdec -B build-mdec-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-mdec-debug --config Debug
ctest --test-dir build-mdec-debug --build-config Debug --output-on-failure

cmake -S clean-aot/mdec -B build-mdec-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-mdec-release --config Release
ctest --test-dir build-mdec-release --build-config Release --output-on-failure
```

The test fixture stream is generated from hand-constructed blocks. It covers
table loads, coefficient saturation, independently calculated RLE/IDCT golden
vectors, output packing and flags, whole-word FIFO backpressure, declared-input
boundaries, DMA accounting, status transitions, canonical/hostile snapshots,
truncation, and deterministic randomized and adversarial schedules.

## Deliberate boundaries

This tranche does not parse BIN/CUE or STR files, decode XA audio, emulate the
DMA controller's global arbitration/chopping, claim undocumented decompression
latency, or provide a host presentation API. Those belong to the machine and
product layers. The device cycle counter is an exact, reproducible accounting
model for this component; it is not yet a measured console-cycle replacement
for the unknown hardware decompression timing. The public documentation also
states that Data-Out DREQ clears after an unknown number of words from a block;
until native measurements establish that threshold, this model deliberately
uses a level request while output data is available. An empty MDEC0 read is
reported to the machine layer as `OutputFifoEmpty` instead of inventing a
particular garbage/open-bus value.
