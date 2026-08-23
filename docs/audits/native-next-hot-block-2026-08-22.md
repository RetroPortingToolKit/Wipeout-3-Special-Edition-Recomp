# Next native-performance candidate after `0x807814D0`

Date: 2026-08-22

## Finding

After the captured `0x807814D0` continuation, the next bounded candidate is
the HD texture-replacement lookup/cache path: hot replacement lookup, memo
behavior, and invalidation of tracked uploads. This is a render-side
hypothesis, not proof that it is the current PAL x2 limiter.

## Evidence

- The retained OpenGL run measured 445 indexed entries, 299 matches, 300
  decoded images, 47,919 replacement hits, and zero decode failures. The path
  is therefore live during the evidenced workload.
- `docs/audits/renderer-quality.md` identifies linear lookup and partial-VRAM
  invalidation as risks without bounded cache/eviction telemetry.
- An earlier HD-on/off A/B still found the route CPU-bound with HD replacement
  disabled, so this cannot be claimed as the sole cause. It is nevertheless a
  useful isolated consumer to measure beside the native-dispatch control.

## Required bounded measurement

Run the same generic `-O3 -DNDEBUG` PAL x2 race window with HD replacement on
and off, keeping guest/native dispatch and presentation settings identical.
Record per frame: lookup calls, candidate entries scanned, memo hits/misses,
replacement misses, decoded/uploaded bytes, invalidations, evictions,
emulation/PGXP time, lookup/cache time, presentation time, guest Hz, and audio
underruns/overflows.

Promote this candidate only if the delta repeats across three matched runs and
materially consumes the missing 10 ms PAL x2 budget. Otherwise profile PGXP/GPU
geometry or remaining guest interpreter bridges.
