# Semantic audit: `0x80160F1C`

## Result

`0x80160F1C` is the PsyQ-style synchronous CD-ROM adapter `CdControl`.  It
performs status/preflight recovery around the requested command, temporarily
suppresses the CD sync callback, retries at most four times, and normalizes the
lower command core's result to boolean success.

## Boundary, ABI, and return

The routine opens a `0x38`-byte frame at `0x80160F1C`, saves `ra`, `fp`, and
`s0`--`s7`, and returns through one epilogue.  `jr ra` is at `0x80161050`; its
delay slot restores `sp` at `0x80161054`.  The exact half-open interval is
`[0x80160F1C,0x80161058)`, `0x13C` bytes, matching the generated range.

The recovered public ABI is:

```c
int CdControl(uint32_t command, const void *parameters, void *result);
```

Only `command & 0xFF` is retained.  The buffers are forwarded without being
dereferenced by this owner.  A requested transaction whose lower-level result
is zero returns 1 immediately; four failed attempts return 0.  Low-level error
codes are not exposed.

## Command sequence and retry contract

The sole callee is confirmed `IssueCdCommand` (`0x80168080`), always with
`a3 = 0`.  Each attempt can issue up to three transactions:

1. If the requested command is not 1 and status byte `0x8017B130` has bit
   `0x10`, issue `(1, NULL, NULL, 0)`.
2. If `parameters != NULL` and word
   `0x801792D8[command & 0xFF]` is nonzero, issue
   `(2, parameters, result, 0)`.  A nonzero result fails the attempt before
   the requested command.
3. Restore the callback word, then issue
   `(command & 0xFF, parameters, result, 0)`.

The local counter starts at 3 and failed paths decrement through `3,2,1,0`
before stopping at `-1`, proving a maximum of four attempts.  Command-1
recovery's result is not tested in this wrapper; the preflight and requested
transactions control retry.  Per-command register/IRQ timeouts live in
`IssueCdCommand`, not in this bounded wrapper.

## Callback and hardware behavior

The word at `0x8017B124` is saved at entry.  At the start of every attempt the
routine writes zero there, suppressing the installed sync callback during
recovery/preflight.  It restores the saved value before every requested
transaction and after final exhaustion.  Thus all exits preserve the caller's
preexisting callback word.

`CdControl` does not directly touch the CD-ROM MMIO block.  `IssueCdCommand`
owns parameter writes to `0x1F801802`, command issue through `0x1F801801`, CD
IRQ-phase servicing, response copying, and its VBlank/spin watchdogs.  This
owner's hardware semantics are sequencing, callback isolation, bounded retry,
and boolean result normalization.

The related `CdControlWithRetry` at `0x8016118C` performs the same broad
preflight protocol but has an additional exhausted-retry classifier at
`0x80167B38`; this function has no such call and returns plain success/failure.

## Exhaustive callers and interior targets

An aligned EXE scan finds exactly 12 entry calls:

```text
80118944 8011895C 80118A54 80118A6C 80118C70 80118C88
80160958 80160970 801609B8 80163D14 80163D64 80164760
```

Grouped by owner:

- `0x80118588`: six calls;
- `0x8016088C`: three calls;
- `0x80163CF8`: two calls;
- `0x80164734`: one call.

No external J or JAL targets an interior instruction, so the owner contains no
hidden callable alias.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS,
  exhaustive caller/interior scans, and command sequencing;
- `generated/SCES_028.45_full.ranges` and the generated owner;
- `docs/audits/semantic-80168080.md` for the independently proven CD command
  core and MMIO/timeout behavior;
- `docs/audits/semantic-8016118c.md` for the independently recovered related
  wrapper and its distinct exhausted-retry classifier.
