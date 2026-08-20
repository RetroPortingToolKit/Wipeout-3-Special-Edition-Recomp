# Clean fixed-point SPU device

This directory is a standalone C++20 PlayStation SPU device boundary. It has
no host clock, threads, locks, I/O, sleeps, dynamic allocation, media policy,
or dependency on another component. `Device::render` consumes a
caller-owned bounded stereo span and advances exactly one 44,100 Hz sample per
output entry, so the machine timing component can supply its exact 25/50/100 Hz
sample budgets without a second clock.

Implemented surface

- 24 voices with key-on/key-off, start/repeat/current positions, pitch,
  channel volume fixed and sweep modes, ADSR attack/decay/sustain/release,
  noise, pitch modulation, end flags, saturation, and stereo mixing. Key-off
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
  callback signaling.
- 512 KiB sound RAM, a 32-halfword transfer FIFO, manual transfer writes,
  typed bounded DMA spans/callbacks, transfer modes 0/1/2/3, transfer
  replication modes, transfer status, a stable programmed transfer-address
  register with a separate advancing internal pointer, and IRQ checks on RAM
  transactions.
- Separate bounded CDDA, resampled-XA, and external stereo FIFOs. Pushing past
  capacity drops the newest samples and records overflow; consuming an empty
  enabled input produces a deterministic zero only as an explicitly recorded
  underflow. `push_xa_resampled` accepts stereo PCM already resampled to
  44,100 Hz. XA sector parsing and sample-rate conversion belong to the
  CD-ROM component and are not hidden in this boundary.
- A fixed-point, register-driven reverb path with same-side reflection,
  cross-side reflection, four comb stages, two all-pass stages, wet output,
  intermediate saturation, and a 22,050 Hz work-area cadence. The silicon FIR
  work-area resampler and every device-specific read/write timing detail are
  not claimed; callers requiring those details must keep this tranche behind an
  explicit audio policy decision.
- Canonical little-endian snapshots containing all device state and sound RAM,
  payload validation, FNV-1a replay digests, and callback-independent restore.
- Relative MMIO offsets accept only legal aligned 16-bit operations. Malformed
  widths, unaligned offsets, unmapped offsets, read-only/write-only accesses,
  value overflow, and transfer FIFO boundaries return explicit results.

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
edges, all envelope phases and representative rates, sweep volumes, noise and
modulation ordering, saturation, masks and malformed MMIO, transfer FIFO and
DMA boundaries, capture, CD/XA/external underflow and overflow, chunking,
snapshot replay, and long deterministic generation. The benchmark runs a
representative 24-voice/reverb workload and reports measured samples per
second; its timing is evidence only and is not a correctness gate.

Sanitized validation uses the same source tree:

```text
cmake -S clean-aot/spu -B build-clean-spu-clang-asan -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCLEAN_AOT_SPU_ENABLE_SANITIZERS=ON
cmake --build build-clean-spu-clang-asan --config RelWithDebInfo
ctest --test-dir build-clean-spu-clang-asan --build-config RelWithDebInfo --output-on-failure
```

Integration risks remaining for a later tranche are exact hardware ADSR corner
cases outside the covered rate vectors, device-specific reverb read/write
timing, raw XA sector decode, external-port production wiring, and the system
interrupt-controller adapter.
