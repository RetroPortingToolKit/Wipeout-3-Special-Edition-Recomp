# Semantic audit: `0x8015FA7C`

## Result

`0x8015FA7C` is the confirmed VSync-private polling helper
`WaitForVBlankCountWithWatchdog`. It waits until the signed software VBlank
counter reaches a target, while decrementing a caller-scaled busy-loop
watchdog. On watchdog expiry it prints `VSync: timeout`, invokes two BIOS
recovery services, and returns early without forcing the target count.

Its recovered ABI is:

```c
void WaitForVBlankCountWithWatchdog(
    int32_t target_vblank,
    int32_t timeout_scale);
```

It has no meaningful return value.

## Boundary and callers

The helper opens a 32-byte frame at `0x8015FA7C`, saves `ra`, and routes its
initially-satisfied, normally-completed, and timed-out paths to the common
restore at `0x8015FB04`. It returns at `0x8015FB0C..0x8015FB10`. The next
independent leaf begins at `0x8015FB14`. Therefore its exact interval is
`[0x8015FA7C,0x8015FB14)`, or `0x98` bytes, matching the generated range.

An exact aligned JAL scan finds only two callers, both inside `VSync`:

```text
0x8015F9B4  first requested-frame-distance wait
0x8015F9D8  final next-VBlank boundary wait
```

There are no overlay or unrelated subsystem callers. Neither call consumes a
scalar return.

## Poll and watchdog algorithm

The software counter is the signed word `gVBlankCount` at `0x8017B0FC`. The
entry check and every loop check use signed comparison:

```c
if (gVBlankCount >= target_vblank)
    return;

int32_t watchdog = timeout_scale << 21;
for (;;) {
    watchdog--;
    if (watchdog == -1) {
        timeout_recovery();
        return;
    }
    if (gVBlankCount >= target_vblank)
        return;
}
```

The counter is reloaded on every poll and is expected to advance through the
VBlank interrupt path. This routine never writes or increments it. A scale of
zero times out on the first decrement. For positive scale `N`, expiry occurs
after `N*2^21 + 1` decrements. The implementation uses a 32-bit left shift and
does not guard oversized or negative scales; the two actual callers supply
small nonnegative values derived from VSync modes.

## Timeout and recovery semantics

On the exact watchdog transition to `-1`, the helper performs:

1. `0x801671A8(0x8016F1CC)`, a BIOS B0/3F string-output veneer, with the
   retained NUL-terminated text `VSync: timeout\n`;
2. `0x80162464(0)`, BIOS B0/5B `ChangeClearPAD(0)`;
3. `0x801671C0(3, 0)`, BIOS C0/0A `ChangeClearRCnt(3, 0)`.

It then unconditionally jumps to its epilogue at `0x8015FB04`. It does not
resume polling, synthesize a VBlank, change the target, reset Timer1, or report
success/failure to the caller. This corrects the earlier VSync audit's claim
that the helper continued checking after recovery.

The recovery calls alter BIOS automatic interrupt-acknowledgement behavior for
the pad and root-counter paths. The exact game-visible contract here is the
call triplet and early return; a broader reason for those BIOS settings is not
required to establish the helper semantics.

## Relationship to Timer1 and GPU waits

This helper itself reads no Timer1 register or GPU register. Those operations
belong to its sole owner `VSync`:

- `VSync` samples Timer1 through the pointer at `0x801790C4`, computes the
  entry-time delta against `0x801790C8`, and refreshes that baseline only after
  its wait sequence;
- it snapshots GPUSTAT through `0x801790C0` and, when field/interlace bit
  `0x00400000` is set, waits for GPUSTAT bit 31 to change phase;
- it calls this helper before that GPU field wait to establish software
  VBlank-count boundaries.

For mode zero, VSync's first helper call uses `(saved_vblank, 0)`, normally an
already-satisfied check, and its second uses `(current_vblank+1, 1)`. For mode
`N >= 2`, the first uses `(saved_vblank+N-1, N-1)` and the second again waits
one final boundary with scale one. Thus the watchdog bounds software-counter
polling, while Timer1 measurement and GPU field synchronization remain in the
owner.

If the watchdog returns early, `VSync` continues its owner-level sequence. It
may consequently proceed to the GPU field wait and baseline refresh without
the requested software count having been reached; this is intentional
timeout-recovery behavior, not successful target satisfaction.

## State effects

On normal completion the helper has no persistent writes at all. On timeout,
its only persistent effects are whatever the three BIOS veneers perform. Its
watchdog is stack-local, and the message pointer refers to immutable executable
data. It never updates `gVBlankCount`, Timer1 baselines, saved-frame state, or
GPU state directly.

## Evidence

- `disc/SCES_028.45`, exact MIPS disassembly, caller scan, BIOS veneer
  selectors, and timeout string bytes;
- `generated/SCES_028.45_full_52.c`, complete loop and all exits;
- `generated/SCES_028.45_full.ranges`, exact `0x98` range;
- `docs/audits/semantic-8015f904.md`, independently recovered VSync Timer1,
  GPU, mode, and call-argument semantics;
- `docs/audits/semantic-80162474.md`, independent identification of B0/5B as
  `ChangeClearPAD`.
