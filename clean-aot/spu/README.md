# Clean fixed-point SPU device

This directory is a standalone C++20 PlayStation SPU device boundary. It has
no host clock, threads, locks, I/O, sleeps, dynamic allocation, media policy,
or dependency on another component. `Device::render` consumes a
caller-owned bounded stereo span and advances exactly one 44,100 Hz sample per
output entry, so the machine timing component can supply its exact 25/50/100 Hz
sample budgets without a second clock.

Clock-domain contract

- The SPU output clock is always `44,100 Hz`. PAL x2 changes the machine update
  cadence to `100 Hz`; it does not overclock the SPU.
- `Device::render_100hz_update` accepts a fixed-extent span of exactly `441`
  stereo samples. The 25 Hz and 50 Hz budgets are `1,764` and `882` samples.
- CPU retirement multiplication belongs to the CPU/machine scheduler. No CPU
  frequency, cycle count, or retirement multiplier enters this API, so it
  cannot alter SPU sample count, ADPCM pitch, noise frequency, ADSR rate, or
  capture cadence.

Implemented surface

- 24 voices with key-on/key-off, start/repeat/current positions, pitch,
  channel volume fixed and sweep modes, ADSR attack/decay/sustain/release
  including the maximum-sustain no-decay edge,
  threshold-consuming envelope counters, noise, pitch modulation, end flags,
  saturation, and stereo mixing. Key-off
  and end-mute silence the envelope while retaining the bounded RAM scan state
  needed for sound-RAM IRQ behavior.
- 16-byte, 28-sample ADPCM blocks with predictor/filter reconstruction,
  loop-start/loop-end handling, repeat redirection, and sound-RAM IRQ checks.
- A fixed-width four-tap interpolation table entered as independent integer
  behavior constants. It uses the documented 512-entry PS1 coefficient table;
  the four taps sum to `0x7f80` within the documented per-phase rounding, with
  no host-math dependency.
- Main, reverb, CD, and external volume controls, SPU enable/mute, CD/external
  enable and reverb routing, capture buffers, capture-half status, and IRQ
  callback signaling. CD and voice capture writes run every sample; transfer
  control bits gate capture IRQs/status, not the writes themselves.
- 512 KiB sound RAM, a 32-halfword transfer FIFO, manual transfer writes,
  typed bounded DMA spans/callbacks, transfer modes 0/1/2/3, transfer
  replication modes, transfer status, a stable programmed transfer-address
  register with a separate advancing internal pointer, and IRQ checks on RAM
  transactions. The delayed SPUSTAT DMA read/write request is exposed as the
  live whole-machine DMA4 DRQ level; stop mode and read warm-up deassert it.
- Separate bounded CDDA, resampled-XA, and external stereo FIFOs sized to hold
  the legal worst-case 9,408-frame mono/half-rate XA sector burst. Pushing past
  capacity drops the newest samples and records overflow; consuming an empty
  enabled input produces a deterministic zero only as an explicitly recorded
  underflow. `push_xa_resampled` accepts stereo PCM already resampled to
  44,100 Hz. XA sector parsing and sample-rate conversion belong to the
  CD-ROM component. `CdAudioSink` is the explicit native-time integration seam:
  it preserves XA decode/resampler history across forward interleaved sectors,
  resets on stream identity/format discontinuities, applies the staged CD
  matrix and mute state, resamples through the documented seven-phase XA
  zigzag filter, and hands bounded 44.1-kHz frames to this device.
- A fixed-point, register-driven reverb path with same-side reflection,
  cross-side reflection, four comb stages, two all-pass stages, wet output,
  intermediate saturation, and the documented alternate left/right 22,050 Hz
  work-area cadence. Work-area writes require the documented normal sound-RAM
  transfer-control value; other values retain reads/output and cursor cadence
  without mutating RAM. The documented 39-tap input and output FIRs are modeled
  with their 19-sample causal delays, giving the documented 38-sample combined
  delay at the 44,100 Hz output. Reverb writes are saturated at the documented
  datapath boundaries, including the documented `vIIR=-8000h` behavior.
  The public measurements still leave exact silicon evaluation order and the
  output FIR polyphase convention underdetermined; this implementation uses a
  centered zero-stuffed polyphase reconstruction and does not claim bit-exact
  wet audio. With reverb writes disabled, APF coefficient gates prune the
  documented upstream APF/COMB RAM reads and their IRQ visibility.
- Canonical little-endian snapshots containing all device state and sound RAM,
  payload validation, FNV-1a replay digests, and callback-independent restore.
  Callbacks are synchronous/non-owning, may not re-enter the device, and remain
  attached to the destination object rather than becoming snapshot data.
- Relative MMIO offsets accept only legal aligned 16-bit operations. Malformed
  widths, unaligned offsets, unmapped offsets, read-only/write-only accesses,
  value overflow, and transfer FIFO boundaries return explicit results. The
  documented unknown R/W scratch ranges retain deterministic 16-bit values;
  the genuinely undefined read-only status returns a stable zero.

The device path uses only fixed-width integer arithmetic. Signed shifts are
implemented by explicit floor arithmetic, intermediate products use bounded
64-bit values, and all externally visible samples and reverb writes saturate
before narrowing. The render loop is bounded by `MaxRenderSamplesPerCall` and
does not allocate.

Build and test

```text
cmake -S clean-aot/spu -B build-clean-spu-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build-clean-spu-gcc --config Release
ctest --test-dir build-clean-spu-gcc --build-config Release --output-on-failure
build-clean-spu-gcc/clean_aot_spu_benchmark
```

The independent test executable covers ADPCM predictor vectors, loop and IRQ
edges, exact Gaussian phase anchors, all envelope phases and rate freezes,
the maximum-sustain attack boundary,
fresh-divider sweep restarts, the inverted all-ones sweep,
phase-invariant exponential decrease,
continuously clocked noise, modulation ordering,
saturation, masks and malformed MMIO, transfer FIFO/DMA modes,
always-on capture and IRQ gates, CD/XA/external routing, chunking, adversarial
snapshot validation, 39-tap reverb FIR phase and combined-delay behavior, long
transfer-gated reverb writes, disabled-reverb read/IRQ pruning, deterministic
generation, and a fixed
441-sample 100 Hz boundary under varying
simulated CPU retirement multipliers. The benchmark runs a representative
24-voice/reverb workload and reports measured samples per second; its timing is
evidence only and is not a correctness gate.

Sanitized validation uses the same source tree:

```text
cmake -S clean-aot/spu -B build-clean-spu-clang-asan -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCLEAN_AOT_SPU_ENABLE_SANITIZERS=ON
cmake --build build-clean-spu-clang-asan --config RelWithDebInfo
ctest --test-dir build-clean-spu-clang-asan --build-config RelWithDebInfo --output-on-failure
```

The implementation provenance and exact pinned behavioral references are in
[`provenance.md`](provenance.md).

Integration risks remaining for a later tranche are sub-sample MMIO and
key-on/key-off timing beyond the native 44,100 Hz sample boundary, exact
silicon reverb evaluation/polyphase rounding, the unknown third per-voice RAM
read and complete bus-slot IRQ ordering, physical transfer FIFO/DMA request
  physical transfer bus-slot timing and arbitration, residual XA zigzag silicon rounding, external-port
production wiring, and the system interrupt-controller adapter. The known
delayed SPUCNT low bits, DMA-read request warm-up, inactive-voice header/data
reads, and synchronous FIFO behavior are implemented and tested; the
unresolved items remain explicit acceptance gates.
