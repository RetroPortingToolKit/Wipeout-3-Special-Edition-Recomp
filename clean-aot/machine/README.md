# Clean deterministic machine timing core

This directory is a standalone C++20 machine-clock slice. It is intentionally
independent of the legacy runtime and contains no renderer, audio device,
thread sleep, host pacing, disc handling, or game policy.

The machine has one exact, unscaled native timebase: a VBlank at 100, 50, or
25 Hz advances one, two, or four quanta at 1/100 second per quantum. SPU and
CDDA remain on that native timeline. CPU retirement is a separate immutable
exact rational multiplier selected at construction. The stock constructor is
`1/1`; multipliers are reduced and must be positive, no greater than `10/1`,
including full-width 64-bit rationals. No floating point or dynamic/adaptive
multiplier is used.

| Domain | Native rate | 100 Hz frame | 50 Hz frame | 25 Hz frame |
| --- | ---: | ---: | ---: | ---: |
| R3000A at `1/1` | 33,868,800 cycles/s | 338,688 | 677,376 | 1,354,752 |
| R3000A at `9/1` | CPU retirement only | 3,048,192 | 6,096,384 | 12,192,768 |
| SPU | 44,100 samples/s | 441 | 882 | 1,764 |
| CDDA | 75 sectors/s | rational | rational | 3 |

At 100 Hz, CDDA sector receipts are emitted as `0, 1, 1, 1` repeatedly, so
every four frames contain exactly three sectors. At 50 Hz they alternate
`1, 2`; at 25 Hz each frame emits three. The native accumulators and fractional
CPU multiplier carry are preserved across VBlank mode changes, so no floating
point conversion or drift is introduced.

`machine_c.h` is the narrow C event ABI. `machine.hpp` provides the small C++
wrapper. `Machine(VBlankRate)` remains the compatible stock `1/1`
constructor; the overload accepting `CpuMultiplier` or numerator/denominator
is immutable after construction. The C factory
`clean_aot_machine_create_with_cpu_multiplier` returns `NULL` for invalid input.
`clean_aot_machine_canonicalize_cpu_multiplier` exposes the same fail-closed
validation and canonical reduction without allocating a machine.

Every event and frame receipt carries the canonical multiplier and an explicit
CPU-retirement total. The fixed FNV-1a byte-order digest includes those fields,
so otherwise identical native schedules at different CPU multipliers cannot
share evidence. A pending mode change is emitted immediately before the next
frame's VBlank event. MODE_SWITCH and frame-start VBLANK expose the retirement
total before that frame's CPU budget; CPU_CYCLES and later domain events expose
the total after it. Public C and C++ identifiers use the neutral
`clean_aot_machine` / `clean_aot::machine` names; title policy remains outside
this generic component.

Integration boundary: the CPU multiplier increases only the exact number of
CPU cycles the static recompiled code may retire during the native interval.
It must not make SPU samples, CDDA sectors, DMA/device deadlines, or other
native devices run faster. A runtime may either convert each native-device
deadline into the CPU timeline with exact carry or keep a separate native
master timeline. The receipt exposes the native `timebase_ticks` and elapsed
timebase together with CPU retirement totals, multiplier, SPU total, and CDDA
phase so either integration is deterministic. This module does not modify
CDROM, SPU, or SoC scheduling.

Rate advancement uses checked quotient/remainder binary accumulation and never
forms a potentially overflowing numerator product. Cumulative totals can
eventually exhaust their public unsigned counters; a C++ step throws
`std::overflow_error` before mutating or emitting anything, while the C ABI returns
`CLEAN_AOT_MACHINE_STATUS_OVERFLOW`. No wraparound is silently used as timing
policy.

Build and run:

```text
cmake -S clean-aot/machine -B build-clean-machine -DCMAKE_BUILD_TYPE=Release
cmake --build build-clean-machine --config Release
ctest --test-dir build-clean-machine --build-config Release --output-on-failure
```

The C++ test executable performs stock and `9/1` 100 Hz checks, exercises the
`133/100` fractional carry over one hour with no drift, verifies native SPU and
CDDA independence, checks the inclusive `10/1` boundary and full-width
rationals, checks reset identity and mode switches, compares repeated
receipts/events and multiplier-sensitive digests, and exercises invalid C ABI
inputs. A separate C translation unit links against the C++ library and runs
through the public C ABI.
