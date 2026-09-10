# Semantic audit: `0x8016C51C` / `ArmTimer2Timeout`

## Result and ABI

`0x8016C51C` arms the shared library timeout state by recording a caller
threshold and a snapshot of the PlayStation Timer 2 counter:

```c
void ArmTimer2Timeout(uint32_t threshold);
```

Only `a0` is consumed.  The timer sample remains incidentally in `v0`, but no
caller reads it and the leaf's contract is the two global stores, so it has no
semantic return.

## Exact boundary and incoming targets

The generated range assigns `[0x8016C51C, 0x8016C53C)`, exactly `0x20` bytes,
to this frameless eight-instruction owner.  It returns at `0x8016C534` and
performs its second global store in the return delay slot at `0x8016C538`.
The next independent owner starts at `0x8016C53C`.

An aligned scan of the authenticated executable finds exactly twelve direct
JAL sites:

```text
801694D0 80169640 80169670 801696BC 801696EC 80169738
80169834 80169B2C 80169C6C 8016C930 8016CAB4 8016CB60
```

There are no JALs to an interior instruction.  Caller thresholds are `0x3C`,
`0x50`, `0x91`, `0x190`, `0x1AE`, and `0x7D0`; most calls precede a library
service/poll loop, and one site immediately invokes the paired timeout test at
`0x8016C53C`.

## State and hardware behavior

The leaf performs three operations in this exact order:

1. read unsigned halfword `0x1F801120`, the current Timer 2 count;
2. store `threshold` unchanged as a 32-bit word at `0x801EEBC4`;
3. store the zero-extended counter snapshot as a 32-bit word at `0x801EEBC8`.

It does not write Timer 2, inspect its mode, enable an interrupt, mask the
threshold, or validate zero/large thresholds.  Calls overwrite the single
shared timeout pair, so this API supports one active deadline rather than
nested independent timers.

The adjacent owner at `0x8016C53C` proves the interpretation.  It rereads
Timer 2 count and the two saved words, accounts for 16-bit wrap using the
Timer 2 target register at `0x1F801128`, examines mode bit `0x200` at
`0x1F801124` to select raw or `>>3` elapsed units, and returns whether the
stored threshold has expired.  Those checks belong to the paired poller, not
to this arming leaf.

## Caller context

The first nine callers are in the adjacent low-level device/library region;
the last three are in the pad/SIO transaction driver.  This cross-subsystem
use is why the symbol describes the Timer 2 mechanism rather than naming a CD,
pad, or SIO-specific timeout.  Apart from the Timer 2 read and two shared-word
writes, the function has no memory, device, control-flow, or synchronization
effect.

## Confidence

`confirmed`: exact boundary, one-argument/no-return ABI, exhaustive caller and
interior-target census, Timer 2 register identity, global state addresses and
store ordering, threshold values, and paired-poller relationship are
instruction-proven.
