# Clean AOT architecture

## Product definition

This project is a native remaster produced by static/region recompilation. It
is not an emulator wrapper. Players provide and locally verify their own
SCES-02845 Redump BIN/CUE and BIOS. Setup applies the pinned hueponik PAL patch,
discovers executable identities offline, and builds or selects only known AOT
regions. No copyrighted disc image, retail BIOS, or patched executable ships.

## Dual-lane transition

```text
quarantined legacy lane                 clean shipping lane
psxrecomp@d56 + diagnostics             clean-aot/
  route/input discovery                   offline media + executable catalog
  screenshots and behavior receipts       R3000A/GTE region compiler
  coverage hints                           deterministic PSX machine runtime
  never a correctness oracle               enhancement ABI + WO3SE policy
  never linked into release                 no fallback execution
```

Each clean milestone replaces one deterministic legacy milestone: BIOS boot,
intro, language menu, race loading, first race frame, full race, then the full
content matrix. The legacy lane can be deleted when the clean lane covers the
matrix with zero unknown executable identities.

## Component boundaries

- `clean-aot/compiler`: PS-X EXE ingestion, CFG/region IR, dataflow, exact
  guest-to-host lowering, sharded native emission.
- `clean-aot/cpu`: build/test-only architectural semantic oracle. It must not
  appear in a release link map as a general interpreter.
- `clean-aot/catalog`: deterministic offline catalog of executable variants,
  dispatch roots, ranges, hashes, relocations, and source-media provenance.
- `clean-aot/machine`: memory and device-visible event ABI, rational clocking,
  interrupts, DMA, timers, CD-ROM, SIO, MDEC, SPU, GTE and GPU state.
- `clean-aot/render`: renderer-neutral GPU packets, geometry provenance,
  perspective interpolation, texture identity, and presentation interfaces.
- `wo3se-policy` (product layer): SCES-02845 addresses/guards, hueponik mode,
  aspect/culling policy, texture/font manifests, and independently reproduced
  game fixes. None of these constants belong in generic `clean-aot` code.

## Region execution contract

1. A region is a connected guest CFG unit, not an inferred C function.
2. Guest live-ins enter host temporaries once; direct edges and loops retain
   values across basic blocks.
3. RAM fast paths and cycle accumulation are inline. State is materialized only
   at MMIO, interrupts, exceptions, indirect exits, variant switches, and
   explicit diagnostic safepoints.
4. Delay slots, delayed loads, unaligned transfers, overflow, exceptions and
   COP0/GTE visibility are architectural IR semantics—not emitter accidents.
5. Every direct edge is retained even when no function symbol exists. A
   synthetic `region -> trampoline -> continuation -> fallthrough` regression
   must never manufacture `PC=0`.
6. Unknown PCs or executable bytes fail closed in development and release.

## Static executable variants

Setup catalogs each executable image as:

```text
{ guest load range, SHA-256, relocation metadata, dispatch roots,
  executed/code ranges, source media identity }
```

Runtime DMA or guest writes may activate only an exact precompiled identity.
The same address holding A, then B, then A again remains three epochs selecting
two immutable variants. Stack/data bytes are never promoted to code merely
because they occupy the candidate address window. A branch delay slot crossing
a page/region boundary belongs to the same validated identity as its branch.

## Timing contract

- Nominal R3000A clock: 33,868,800 Hz. CPU retirement has an independent,
  exact rational multiplier; device and media clocks do not inherit it.
- SPU output: 44,100 samples/s.
- CDDA: 75 sectors/s.
- PAL x2 doubles the CRTC/video clock from 50 to 100 Hz without doubling the
  SPU or CD-ROM clocks. It is a simulation cadence, not frame duplication.
- At a 100% CPU multiplier, 25/50/100 Hz provide 1,354,752 / 677,376 /
  338,688 CPU cycles per VBlank. The selected product multiplier scales only
  those CPU budgets and must be fixed by measured race acceptance, never by
  host-loop speed or an adaptive controller.
- 25/50/100 Hz produce 1,764 / 882 / 441 SPU samples per VBlank regardless of
  CPU multiplier.
- At 100 Hz, exactly three CDDA sectors span four simulation frames.
- Every generated memory transaction that can reach MMIO carries its raw
  architectural issue cycle across the native ABI. A generated shard may keep
  registers resident, but it may not hide that timestamp in private state or
  expose a stale caller snapshot to timers, SIO, DMA, GPU, CD-ROM, or SPU.
  Device deadlines stop retirement at an instruction boundary; the scheduler
  advances native device time, updates interrupt lines, and only then resumes
  generated code. Wait states are charged once after the transaction and never
  converted into device time by the generated region.

The host presenter consumes completed simulation frames; it does not invent
duplicate internal updates. A 3,600-frame 100-Hz proof therefore requires 3,600
distinct game advances, exactly 1,587,600 SPU samples, and 2,700 CDDA sectors
of timeline advancement. Its expected CPU-cycle total is
`1,219,276,800 * multiplier_numerator / multiplier_denominator`, with exact
rational carry. The product multiplier must be high enough for hueponik's
100-FPS/max-detail update to finish every 10 ms simulation interval at 1.00x,
then remain pinned and covered by deterministic receipts.

## Enhancement contract

- Native 4:3 is the identity baseline. 16:9, 21:9 and 32:9 use explicit camera,
  projection and culling policy; menus and FMV have declared composition modes.
- PGXP-class geometry carries source identity and generation through GTE values,
  RAM and GPU packets to each triangle vertex. A hook count is not proof.
- Texture replacement identity includes source pixels, CLUT, dimensions, UV
  region and upload generation.
- Fonts and banners are independently rebuilt assets with source, permission,
  transform and hash provenance.

## Release gates

- Generic-source scan: no title names, serials, guest addresses or policy.
- Compiler semantics: directed and randomized differential tests.
- Static completeness: 100% executed-PC/variant coverage; no interpreter,
  runtime compiler or unknown executable write.
- 4:3 identity: matching timing, GP0 stream, RAM checkpoints and audio timeline.
- 100 Hz: 3,600 unique race updates with exact clock/sample/sector totals.
- Performance: fixed-race interleaved runs, p99.9 below 10 ms on the target PC,
  zero missed presents and audio underruns in an extended soak.
- Link-map scan: no dirty interpreter, GCC/TCC, debug server, oracle, trace,
  capture compiler, or title-specific generic symbol.
- Relocated package test using only player-provided verified media.
