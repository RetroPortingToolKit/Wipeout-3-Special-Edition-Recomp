# Static-AOT performance profile

This profile uses the authenticated 104,002-entry main image and 31,827-entry
NFE image (135,829 entries total). It is a build/runtime engineering receipt,
not executable-coverage authority.

## Deterministic structure

The compact binding stores each hot lookup as an eight-byte
`GeneratedPackedRegionEntry`, versus 24 bytes for the pointer-bearing legacy
entry on the supported 64-bit toolchain. Region function pointers occur once
per generated region. A checked eight-byte range per 4 KiB guest page bounds
binary search to the active page. Binding rejects mixed representations,
invalid function indexes, non-contiguous page ranges, misplaced entries,
identity mismatches, and unknown PCs.

For the two authenticated images, regenerated source changed as follows:

| Measure | Legacy | Compact | Change |
| --- | ---: | ---: | ---: |
| Total generated source | 94,369,306 B | 70,614,937 B | -25.2% |
| Entry-table source | 19,968,115 B | 4,527,118 B | -77.3% |
| Main entry-table object | 3,965,329 B | 1,269,185 B | -68.0% |

Generated region bodies no longer duplicate IR edge metadata that is consumed
only while emitting control flow. The generated native instructions and
runtime trace calls are unchanged.

## Bounded local benchmark

`clean_aot_runtime_dispatch_benchmark` constructs exactly 135,829 contiguous
entries and performs 1,000,000 deterministic pseudorandom lookups through both
representations, requiring equal status, register state, instruction counts,
and checksum. It also bounds epoch and CFI measurements to 100 full
reauthentication operations. On the 2026-08-21 Windows GCC build:

| Measure | Legacy | Compact/page-indexed |
| --- | ---: | ---: |
| Dispatch, 1,000,000 lookups | 175.390 ms | 107.292 ms |
| Mean dispatch | 175 ns | 107 ns |
| Lookups in a 10 ms / 100 Hz host interval | 57,142 | 93,457 |
| Full binding validation | 0.232 ms | 0.586 ms |

One hundred identity/epoch selections took 65.717 ms and one hundred CFI
transfers, including full target-binding validation, took 76.612 ms. These are
control-path costs; no unsafe cache was introduced. The generated-table
compile fell from 1.18 s to 0.62 s and its partial link from 0.05 s to 0.04 s.
The largest region body remains the dominant translation-unit compile cost
(about 94 s locally), so bounded body sharding is the next build-scalability
target. It must preserve exact delay-slot and cross-shard edge behavior.

Run the benchmark with:

```text
cmake --build build-clean-aot --target clean_aot_runtime_dispatch_benchmark
build-clean-aot/runtime/clean_aot_runtime_dispatch_benchmark.exe
```
