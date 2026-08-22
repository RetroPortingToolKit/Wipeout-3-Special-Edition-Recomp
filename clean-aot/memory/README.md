# Clean-room PS1 memory and bus core

This directory is a standalone C++20 memory semantic component for a static or
region-recompiled PS1 program. It owns mutable RAM, scratchpad, and an immutable
copy of the caller-supplied BIOS; hardware transactions cross an explicit
caller-owned router. It contains no interpreter, runtime compiler, device
implementation, renderer, disc reader, title policy, or compatibility path.

The implementation was independently written from the architectural contract.
Sony's PlayStation hardware documentation was used to check the fixed logical
map and alignment rules. PSX-SPX was consulted only as a public behavior clue;
no implementation code or wording was copied from either source.

## Address contract

`translate_address()` accepts only the PS1 direct windows:

| Segment | Virtual range | Translation |
| --- | --- | --- |
| KUSEG direct window | `00000000`-`1FFFFFFF` | physical address unchanged |
| KSEG0 | `80000000`-`9FFFFFFF` | low 29 bits, cached identity retained |
| KSEG1 | `A0000000`-`BFFFFFFF` | low 29 bits, uncached identity retained |

The upper 1.5 GiB of KUSEG and KSEG2/KSEG3 are reported as unsupported. This
component has no TLB or cache-control register implementation. CPU privilege
checks are also outside the bus: the CPU lane must reject user-mode access to
kernel segments before or while converting the returned fault into architectural
state. `segment_address()` returns `std::nullopt` for an unsupported segment or
a physical address outside the direct 512 MiB window; it never aliases invalid
input to low RAM.

The physical map owned or routed here is:

| Physical region | Behavior |
| --- | --- |
| First 8 MiB | 2 MiB main RAM storage repeated four times |
| Scratchpad | 1 KiB at `1F800000`, reachable through KUSEG/KSEG0 only |
| Hardware aperture | `1F801000` through the byte before the BIOS; each transaction is offered to `MmioRouter` |
| BIOS | exactly 512 KiB at `1FC00000`, copied at construction and read-only |

The hardware aperture intentionally includes register holes and expansion
interfaces; the concrete router decides whether each exact address is handled,
unmapped, or rejected. Expansion region 1 and the gap around scratchpad remain
unmapped in this tranche. Region checks are non-overlapping, so RAM, scratchpad,
hardware, and BIOS cannot steal bytes from one another.

The caller can switch the physical RAM aperture between the reset/`0B88h`
8 MiB geometry (four aliases of the retail 2 MiB chip) and the corrected
`0888h` 2 MiB geometry. Changing the aperture never copies or clears RAM.

## Access semantics

`read8/read16/read32` and `write8/write16/write32` are little-endian and
width-correct. The generic methods validate width and origin, enforce strict
halfword/word alignment, check address arithmetic, translate every byte, and
reject a cross-device transaction before any side effect. Valid but absent
physical locations return `Unmapped`; unsupported virtual segments retain the
distinct `UnsupportedSegment` status and valid physical fault address where one
exists.

`AccessOrigin::Dma` is a physical bus-master access: cache-segment aliases and
scratchpad are rejected. Direct RAM writes from CPU and DMA share the same code
invalidation. MMIO requests carry the original virtual address, normalized
physical address, exact width, and origin. Read and write values are masked to
that width. Invalid callback status values fail closed as `MmioRejected`.

BIOS bytes are copied into fixed storage, so caller mutation or lifetime cannot
change execution after construction. The optional MMIO router is still a
borrowed interface and must outlive the bus. Router callbacks are `noexcept`;
throwing through that ABI terminates the process. The bus is not thread-safe and
does not serialize callback re-entry.

`reset_mutable_state()` clears RAM, scratchpad, and page generations while
preserving BIOS and the router. It advances an execution epoch before old
region stamps could become current again. The operation is bounded,
allocation-free, and all-or-nothing if the internal identity space were ever
exhausted.

`Bus` owns about 2.5 MiB of fixed storage and is non-copyable/non-movable. Put
it on the heap or in machine-owned storage, not on a small thread stack.

## Static-region invalidation

Each canonical 4 KiB RAM page has a 64-bit generation that is monotonic within
one reset epoch. Every successful scalar RAM write advances each touched page
exactly once,
including same-value writes and writes through another segment or RAM mirror.
Generation arithmetic saturates: a write is rejected before mutation rather
than wrapping a page back to an earlier epoch.

`VariantIdentity` carries the complete 32-byte catalog digest; all-zero means
unselected and is invalid. `capture_code_region()` records that identity, the
bus domain, reset epoch, virtual interval, and generation of every canonical RAM
page touched by the interval. The caller must already have measured the actual
executable bytes and selected the exact catalog identity; this component does
not hash or discover executable content.

A capture is limited to one MiB. An unaligned maximal interval may touch 257
pages, so the fixed stamp has 257 slots. Capture and validation enumerate pages,
not bytes. Validation reconstructs the exact ordered page set and returns
`Current`, `Stale`, `VariantMismatch`, or `Invalid`. Stamps cannot be replayed
against another `Bus`, survive reset, or revive after an A-to-B-to-A byte
sequence. BIOS and scratchpad are not executable RAM in this contract.

The stamp is defensive metadata for trusted generated code, not a cryptographic
capability against malicious code already running in the host process. The
shipping dispatcher must verify media/catalog bytes, select an exact static
artifact, validate its stamp, and fail closed on every other result. This
component has no fallback execution route.

## Integration boundaries and known limits

- The machine scheduler owns time; this component does not model wait states,
  write queues, caches, interrupts, or device timing.
- The CPU lane owns privilege, AdEL/AdES and bus-error exception delivery,
  unaligned instruction semantics, and KSEG2 cache control.
- Generated regions require a reviewed inline RAM fast path that preserves the
  same canonical-page invalidation. Calling the generic mapped bus for every
  statically known RAM load/store is not the intended performance path.
- The DMA lane must use direct physical addresses and must route every RAM
  mutation through this invalidation contract. Device FIFO behavior remains in
  the DMA/device components.
- The catalog lane owns content measurement and the meaning of the 32-byte
  variant identity. Memory stamps alone do not prove that bytes match a digest.
- Expansion region 1, concrete hardware devices, RAM-size register behavior,
  instruction-cache state, and configurable development-hardware RAM are not
  implemented here.
- No serialized snapshot API exists yet. A future restore path must restore RAM,
  scratchpad, and device state, then discard and recapture all ephemeral region
  stamps rather than serializing bus-domain identity.
- No C ABI is provided.

## Build and verification

From the repository root:

```powershell
cmake -S clean-aot/memory -B build-clean-memory-gcc -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build-clean-memory-gcc --parallel 4
ctest --test-dir build-clean-memory-gcc --output-on-failure
```

Warnings are errors by default. Clang plus ASan/UBSan can be selected with:

```powershell
cmake -S clean-aot/memory -B build-clean-memory-clang-asan -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ `
  -DCLEAN_AOT_MEMORY_ENABLE_SANITIZERS=ON
cmake --build build-clean-memory-clang-asan --parallel 4
ctest --test-dir build-clean-memory-clang-asan --output-on-failure
```

Tests cover every direct-segment boundary, all four RAM mirrors, endian widths,
RAM/scratchpad/hardware/BIOS boundaries, owned BIOS immutability, alignment and
fault metadata, CPU/DMA restrictions, all MMIO widths and callback failures,
non-wrapping generation arithmetic, forged and cross-bus stamps, A-to-B-to-A
epochs, reset,
maximal unaligned ranges, mirror-wrapped page sets, and a fixed-seed RAM
round-trip property. The hardened clean-AOT source gate must also pass over the
entire directory.
