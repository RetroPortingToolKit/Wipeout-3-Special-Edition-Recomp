# Semantic audit: `0x8015F904`

## Result

Recommended symbol: `VSync` (`known`, `emit = false`).

This is the title's PsyQ-compatible VBlank synchronizer/query wrapper. Its
single signed argument selects a query or wait mode. It combines the software
VBlank count, Timer1's free-running count, and GPU field-state bits; it is not
a generic root-counter getter.

## Exact boundary and callers

- Entry: `0x8015F904`, with a 40-byte frame saving `s0`, `s1`, and `ra`.
- Exit: `0x8015FA68..0x8015FA7B`.
- Exact size: `0x178` bytes, matching the sole
  `R 8015F904 178` manifest range.
- `0x8015FA7C` begins a separate 0x98-byte polling helper with its own frame
  and `R 8015FA7C 98` range.
- Generated call evidence contains 45 distinct direct call PCs: 40 in the
  main executable and five overlay/expansion copies. Statically visible main
  modes include `-1`, `0`, `3`, `4`, and `20`, with additional sites passing
  an inherited or dynamic argument. Race-session startup calls modes 4 and 0;
  serial/CD-era timing paths heavily use mode `-1`.

## Proven hardware and state

The executable initializes the pointer at `0x801790C0` to GPU status register
`0x1F801814` and the pointer at `0x801790C4` to root-counter Timer1 count
`0x1F801110`. The other state words are:

- `0x8017B0FC`: software VBlank count;
- `0x801790C8`: saved Timer1 baseline;
- `0x801790CC`: saved/last synchronized VBlank count.

At entry the function reads GPUSTAT and then samples Timer1 repeatedly until
two consecutive word loads match. It computes
`timer_delta = (stable_timer1 - timer_baseline) & 0xFFFF` before examining the
mode. The same consecutive-equality loop refreshes the Timer1 baseline after
a successful wait, preventing a changing counter sample from becoming the
next baseline.

## Mode semantics

### `mode < 0`: frame-count query

Return the software VBlank count at `0x8017B0FC` immediately. The computed
Timer1 delta is discarded, and no baseline or saved-frame word is changed.
This is the common `VSync(-1)` timeout-clock query used by serial/CD paths.

### `mode == 1`: scan-position query

Return the entry-time 16-bit Timer1 delta immediately. No polling helper is
called and neither baseline is updated.

### `mode == 0` and `mode >= 2`: synchronization

These modes perform two helper calls:

1. For mode 0, call `0x8015FA7C(saved_vblank, 0)`. For positive mode `N`
   (other than the early-return mode 1), call
   `0x8015FA7C(saved_vblank + N - 1, N - 1)`.
2. Snapshot GPUSTAT, then call
   `0x8015FA7C(current_vblank + 1, 1)` to cross the final VBlank boundary.

Consequently mode 0 synchronizes to the next VBlank, while positive modes
greater than one first approach the requested frame distance and then cross
the final boundary. Afterward, if the pre-wait GPUSTAT had bit `0x00400000`
set, the function also waits for GPUSTAT bit 31 to differ from the saved field
bit. This is field-phase synchronization, not an extra software VBlank-count
increment.

Finally it stores the current software VBlank count to `0x801790CC`, obtains a
stable Timer1 sample for `0x801790C8`, and returns the Timer1 delta captured at
function entry. The return is therefore the elapsed scan/timer position since
the previous saved baseline, not the amount of time spent in the current wait.

## `0x8015FA7C` interaction

The helper receives `(target_vblank, timeout_scale)`. It returns immediately
when signed `gVBlankCount >= target`; otherwise it polls until that relation is
true. Its stack-local watchdog starts from `timeout_scale << 21` and is
decremented on each poll. On the exact transition to `-1`, it invokes the
diagnostic/recovery triplet at `0x801671A8`, `0x80162464(0)`, and
`0x801671C0(3, 0)`, then returns early even if the target remains unmet. It
does not fabricate or directly increment the VBlank count. The prior wording
that said polling continued after recovery was incorrect; the unconditional
jump at `0x8015FAE4` goes to the helper epilogue.

This helper contract explains both calls from `VSync`: the first enforces any
requested multi-frame distance, and the second always waits for the final
next-frame boundary. Replacing either loop with an unconditional return would
change guest timing and IRQ behavior.

## Confidence limits

The boundary, hardware addresses, stable sampling loops, state words, mode
branches, helper arguments, field-bit wait, return arithmetic, and direct-call
counts are instruction-level facts. `VSync` is used as a behavioral/PsyQ API
name; the original linked library's symbol table is not present in the disc
image.
