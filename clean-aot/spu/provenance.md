# Clean-room provenance

This SPU implementation is original C++20 work under
`docs/CLEANROOM_POLICY.md`. Behavioral documents were used to identify public
hardware facts and to construct independent tests. No emulator or legacy
recompiler implementation was used as source material, and no third-party code
is linked or vendored by this directory.

## Pinned behavioral evidence

The review used the local clean checkout of the public PSX-SPX hardware
documentation at commit
`b791ca25b7833b356b28917383f231078762a75d` from
`https://github.com/psx-spx/psx-spx.github.io.git`:

- `docs/soundprocessingunitspu.md`, SHA-256
  `5edd113b603fbb87c82e5270790072c408b2854949415fc64350c5c56a82fe35`
- `docs/cdromformat.md`, SHA-256
  `d391c98d5156b41edb15759e56b21e5619f73b12e2a541d9806f4bb68c981950`

Facts consulted include the 44,100 Hz/768-system-cycle SPU cadence; 24-voice
register behavior; ADPCM predictor, flags, history, and loop rules; the literal
512-entry interpolation coefficients and per-product rounding; pitch
modulation and global noise equations; ADSR and sweep equations; capture RAM,
IRQ, transfer FIFO/DMA, status, and reverb register behavior; and the documented
limits of current reverb precision. The interpolation table and the documented
39-tap reverb FIR coefficients were independently entered as fixed-width
behavioral constants. Documentation prose and implementation code were not
copied.

## Independent evidence in this directory

`tests/spu_tests.cpp` contains integer golden vectors and adversarial state
tests derived from the documented equations, not from another implementation's
output. In particular, it fixes the SPU clock at 44,100 Hz and checks that a
100 Hz machine update always emits exactly 441 samples regardless of simulated
CPU retirement multiplication.

`tests/spu_benchmark.cpp` is a local throughput workload. Benchmark numbers are
performance evidence only; they are not a behavioral oracle.

## Explicit exclusions

Quarantined legacy branches, earlier remaster code, and all emulator
implementation sources are excluded by policy. They were not inspected or
used to author this tranche. No retail BIOS, disc image, game asset, audio
capture, or generated game executable is present in `clean-aot/spu/`.

The documented reverb FIR coefficients, 19+19 sample delay, alternating
left/right schedule, intermediate saturation requirement, and `vIIR=-8000h`
quirk are represented. The exact silicon FIR polyphase convention and the
remaining per-stage evaluation/rounding order are not fully specified by the
public document. The implementation therefore does not claim bit-exact wet
reverb. The third per-voice RAM read, complete RAM-bus arbitration/order, and
physical FIFO/DMA request timing are likewise unresolved and remain explicit
acceptance gates rather than guessed behavior.
