# Clean AOT GPU command front end

This directory contains a generic C++20 GP0/GP1 command and state front end for
a statically recompiled 32-bit console runtime. It owns the retail 1024x512
array of 16-bit VRAM words and emits renderer-neutral, ownership-complete
events. It does not rasterize, execute guest code, dispatch an interpreter, or
compile code at runtime.

The public interface is `include/clean_aot/gpu/gpu.hpp`. Manual bus traffic uses
`write_gp0()`, `write_gp1()`, and `read_gpuread()`. DMA integrations use
`write_dma_word()` and `read_dma_word()`; those two methods enforce GP1(04),
while manual I/O remains legal in every DMA mode as it is on hardware.

## Packet and opcode contract

The parser classifies every GP0 opcode. Supported hardware families are framed
from their actual family bits:

- all `20h..3Fh` polygon combinations preserve flat/Gouraud, triangle/quad,
  texture, transparency, and raw/modulated bits;
- all `40h..5Fh` line encodings are accepted, including aliases where bits 26
  and 24 are ignored by the line state machine;
- all `60h..7Fh` variable, 1x1, 8x8, and 16x16 rectangle combinations are
  accepted;
- every `80h..9Fh`, `A0h..BFh`, and `C0h..DFh` encoding aliases its respective
  VRAM-copy, CPU-to-VRAM, or VRAM-to-CPU family because the lower 29 command
  bits are ignored;
- `E1h..E6h`, cache flush, and IRQ request are explicit events; documented NOP
  mirrors are accepted and discarded immediately, so they cannot consume
  renderer-event capacity or create false FIFO backpressure;
- unknown GP0(03) and GP0(F0h..FFh) fail closed and resynchronize the GP0
  parser rather than guessing a side effect; unsupported GP1 commands fail
  without interrupting an in-progress GP0 packet.

Fixed packets commit only at their exact length. `finish_gp0_stream()` rejects
every incomplete prefix without mutating VRAM or emitting a partial event.
Polylines are bounded to 256 vertices. Their terminator mask is recognized only
where hardware looks for the first word of the next vertex: the vertex slot for
flat lines and the color slot for Gouraud lines. A Gouraud vertex whose ignored
bits happen to match the terminator is retained as vertex data.

Typed primitive events preserve the complete command packet, full 32-bit raw
color and vertex words, UV words, CLUT/page attributes, and explicit presence
bits for optional per-vertex values. A textured polygon's inline page updates
the effective draw-mode state before its immutable render snapshot is taken.
Bits 9-10 and rectangle flips remain sourced only from GP0(E1), as required.
Variable rectangles set `raw_extent_present`; fixed-size rectangles expose only
their effective width/height and do not manufacture a raw extent parameter that
was absent from the packet. Polygon and rectangle packet arrays must be read
through their explicit `raw_word_count`.

## Draw, display, and reset state

GP1(09) is modeled as the late-GPU permission gate for texture disable. It is
not treated as an upper-VRAM-bank switch. GP0(E1).11 and textured-polygon page
bit 11 can set texture disable only while that permission is enabled; a zero
can still clear it. GP1(09) itself does not clear already-active state, and
GP1(00) preserves the permission latch while resetting the effective draw mode.

GPUSTAT exposes the logical synchronous state: effective draw mode and mask
bits, revision-sensitive field/reverse behavior, display mode, IRQ, DMA
direction/request, and command/read/DMA readiness. `set_scanout_state()` gives
the SoC scheduler explicit odd-field and vblank inputs; bit 31 is suppressed in
vblank. The 480-line status bit is active only with interlace.

This component does not simulate execution cycles or physical FIFO occupancy.
`staged_word_count()` reports words retained by the transactional parser, not
the GPU's 16-word hardware FIFO. GPUSTAT ready bits describe whether this
synchronous API can accept the relevant logical input now. A cycle scheduler
must later overlay real command latency, FIFO drain, scanline timing, and DMA
chopping before the runtime can claim timing accuracy.

The reset boundaries are deliberate:

- `power_on_reset()` is a deterministic cold reset that clears this package's
  VRAM. Physical power-on VRAM is not claimed to be zero.
- GP1(00) cancels partial GP0 and VRAM-read state, resets GP1/GP0 control state,
  preserves VRAM and GP1(09), and does not erase events for commands that had
  already completed.
- GP1(01) cancels only the partial GP0 packet. It does not erase the GPUREAD
  register latch or a completed VRAM-to-CPU stream.
- The last GPUREAD word remains latched and can be read repeatedly.

## VRAM transfers and bounded ownership

Fill coordinates and sizes use their distinct hardware masks. In particular,
raw fill widths `3F1h..3FFh` round to 1024 pixels, while `000h` and `400h` yield
zero width. Copy/upload/download sizes use `((raw-1)&mask)+1`, so zero means the
maximum 1024x512 retail extent. X and Y wrap independently.

VRAM-to-VRAM overlap follows the behavior visible in hardware captures: one
source scanline is buffered before that row is written, but rows execute from
top to bottom. Horizontal overlap is therefore protected within a row, while a
downward overlap can feed an earlier destination row into a later source row.
Mask-test/set rules apply per destination halfword to copy and upload; fast fill
ignores mask state.

Odd uploads preserve the complete caller-supplied final data word but ignore its
padding halfword. Odd downloads expose `final_word_valid_halfwords == 1` and
return a zero host sentinel in the unused high half. That zero is explicitly
not asserted to be hardware/open-bus data; `ReadResult::valid_halfwords` carries
the same distinction through GPUREAD. A shipping title must demonstrate that it
does not depend on the padding value or replace the sentinel with captured
hardware behavior.

The completed-event queue is bounded by both 256 events and a total raw-payload
budget large enough for one maximum VRAM transfer. Large transfer events own
their data, and `pop_event()` releases the budget in constant time. This avoids
unbounded memory growth from a valid but adversarial command stream.

## Native renderer and DMA handoff

The native renderer consumes polygon, line, rectangle, environment, and VRAM
events. Primitive events already contain immutable draw-state snapshots and
exact raw attributes; the renderer must not re-read mutable `control()` state.
This package performs no clipping, rasterization, texture sampling, blending,
dithering, mask application for primitives, or PGXP projection.

The optional `IEventExecutor` is the live ordering boundary. When attached,
completed events execute synchronously before their immutable receipt is
queued. Production binds a native rasterizer directly to this front-end's
VRAM, so primitive writes precede immediate GPUREAD and VRAM-copy feedback.
The frame loop consumes receipts into a separate enhanced visual raster and
does not apply native primitives twice. Snapshot assignment preserves both the
attached executor and the address of shared VRAM.

Display control is intentionally synchronous rather than event-queued. The
SoC/scanout owner must inspect or snapshot `control()` immediately after each
accepted GP1 display write if intermediate register transitions matter. A
consumer that polls only once per host frame may observe the final state but
cannot claim hardware ordering for mid-frame display changes.

The DMA/SoC layer is responsible for bus scheduling and should use
`input_phase()` to distinguish command, parameter, polyline, upload-data, and
download-data phases. It must not infer physical FIFO depth from parser storage
or treat host event-queue backpressure as elapsed GPU time.

## Build and verification

From the repository root, keep generated files outside the admitted source
tree:

```text
cmake -S clean-aot/gpu -B build/clean-aot-gpu -DCMAKE_BUILD_TYPE=Release
cmake --build build/clean-aot-gpu --parallel
ctest --test-dir build/clean-aot-gpu --output-on-failure
```

Configure with `-DCLEAN_AOT_GPU_ENABLE_SANITIZERS=ON` for AddressSanitizer and
UndefinedBehaviorSanitizer on supported non-MSVC toolchains. Sanitizer findings
are non-recovering. GCC and Clang both compile production and test code with
warnings as errors.

From the repository root, the shipping source and generated release link map
must also pass the project isolation gates:

```text
python tools/cleanroom/scan_clean_aot.py --source clean-aot/gpu --root .
python tools/cleanroom/scan_release_link_map.py --link-map <release.map> --root .
```

The tests exhaustively classify all 256 GP0 opcodes, reject every incomplete
fixed-packet prefix, exercise every polygon/line/rectangle family, validate
polyline terminator slots and bounds, cover GP1/reset/GPUREAD/DMA transitions,
check revision-specific GPUSTAT behavior, verify fill/copy/upload/download masks
and odd words, reproduce scanline overlap, stress the full transfer payload
budget, and check immutable replay/snapshot ownership. `state_digest()` covers
partial transfer headers and queued render snapshots rather than hashing only
the current register surface.

`FrontEnd` copy construction captures an ownership-complete save-state image,
and copy assignment restores it transactionally: allocation failure leaves the
live destination unchanged. Tests resume snapshots taken during CPU-to-VRAM
payload input, midway through GPUREAD, and inside a polyline, then compare
pinned native state, VRAM, and queued-event digests.

## Evidence boundary and unresolved hardware risks

Behavior was independently implemented from hardware-facing documentation and
empirical test artifacts. PSX-SPX is used only as a behavioral index and is not
a clean-room source. The texture-disable state transitions are checked against
the logged real-console results in
[`JaCzekanski/ps1-tests/gpu/gp0-e1`](https://github.com/JaCzekanski/ps1-tests/tree/master/gpu/gp0-e1),
and overlap ordering is checked against that project's captured
[`vram-to-vram-overlap`](https://github.com/JaCzekanski/ps1-tests/tree/master/gpu/vram-to-vram-overlap)
image. No emulator implementation or external source code was copied.

Still unresolved for a hardware-identity claim: rasterization and command
timing, CLUT/texture-cache behavior, old/prototype GPU rendering differences,
the physical value of odd download padding, GP0(03) and other undocumented
commands, DMA behavior outside vblank, and title-specific hardware traces. The
shipping target is the one-megabyte late retail GPU; arcade/prototype 2 MB VRAM
is intentionally outside this tranche.
