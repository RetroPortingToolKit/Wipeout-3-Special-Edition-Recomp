# Clean deterministic machine timing core

This directory is a standalone C++20 machine-clock slice. It is intentionally
independent of the legacy runtime and contains no renderer, audio device,
thread sleep, host pacing, disc handling, or game policy.

The machine uses an exact 100 Hz rational timebase. A VBlank at 100, 50, or 25
Hz advances one, two, or four timebase quanta respectively. Each domain keeps
an integer remainder with denominator 100:

| Domain | Rate | 100 Hz quantum | 100 Hz frame | 50 Hz frame | 25 Hz frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| R3000A | 33,868,800 Hz | 338,688 cycles | 338,688 | 677,376 | 1,354,752 |
| SPU | 44,100 Hz | 441 samples | 441 | 882 | 1,764 |
| CDDA | 75 sectors/s | 3/4 sector | rational | rational | 3 |

At 100 Hz, CDDA sector receipts are emitted as `0, 1, 1, 1` repeatedly, so
every four frames contain exactly three sectors. At 50 Hz they alternate
`1, 2`; at 25 Hz each frame emits three. The accumulator phase is preserved
when the VBlank rate changes, so no floating-point conversion or drift is
introduced.

`machine_c.h` is the narrow C event ABI. `machine.hpp` provides the small C++
wrapper. Every frame emits VBlank, CPU, SPU, and CDDA events in that order;
VBlank is the frame-start boundary and the domain budgets belong to the
following simulation interval. CDDA events may have amount zero. A pending mode
change is emitted immediately before the next frame's VBlank event. Frame
receipts and the event stream are hashed with a fixed FNV-1a byte order for
deterministic evidence. Public C and C++ identifiers use the neutral
`clean_aot_machine` / `clean_aot::machine` names; title policy remains outside
this generic component.

All per-frame rational numerators are compile-time proven to fit in 64 bits.
Cumulative CPU cycles are the first public counter that can exhaust; at the
nominal clock its unsigned 64-bit range exceeds 17,000 years of continuous
machine time. The one-hour test independently proves exact totals without
floating point or accumulated drift.

Build and run:

```text
cmake -S clean-aot/machine -B build-clean-machine -DCMAKE_BUILD_TYPE=Release
cmake --build build-clean-machine --config Release
ctest --test-dir build-clean-machine --build-config Release --output-on-failure
```

The C++ test executable performs a 3,600-frame 100 Hz run, checks exact CPU/SPU
budgets and CDDA allocation, performs a separate 3,600-frame 100/50/25/100
mode-switch schedule, verifies nonzero fractional phase across rate changes,
checks a one-hour no-drift run, compares repeated receipts/events, and exercises
invalid C ABI inputs. A separate C translation unit links against the C++
library and runs through the public C ABI.
