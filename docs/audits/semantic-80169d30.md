# Semantic audit: `0x80169D30` / `WaitForJoyAckAndClearIrq`

## Result

`0x80169D30` is the bounded JOY/SIO0 acknowledge helper now named
`WaitForJoyAckAndClearIrq`:

```c
bool WaitForJoyAckAndClearIrq(void);
```

It acknowledges the SIO interrupt in `I_STAT`, waits for the JOY status ACK
bit to clear under an already-armed Timer 2 deadline, then resets/acknowledges
the JOY interrupt condition in `JOY_CTRL`.

## Exact boundary and ABI

The complete owner is `[0x80169D30,0x80169DC0)`, exactly `0x90` bytes, matching
the generated range table. It creates a 0x18-byte frame and saves only `ra`.
Both timeout and success paths converge on the restore/return at
`0x80169DB0..0x80169DBC`. The preceding serial-transfer owner returns at
`0x80169D28`; the next independent polling leaf begins at `0x80169DC0`.

No incoming argument is read, so the ABI is `bool(void)`. `v0` is explicitly
zeroed before the timeout branch and explicitly set to one on the success
path. The result is therefore canonical:

```text
0  Timer 2 deadline expired while JOY_STAT bit 7 remained set
1  ACK/status condition cleared and JOY_CTRL was acknowledged
```

## Hardware identities

Two initialized pointer globals establish the exact register blocks:

```text
*(uint32_t *)0x8017B504 = 0x1F801070  I_STAT
*(uint32_t *)0x8017B508 = 0x1F801040  JOY/SIO0 register base
```

Thus the accessed JOY registers are:

```text
base +0x04  JOY_STAT
base +0x0A  JOY_CTRL
```

This is JOY/SIO0 at `0x1F801040`, not SIO1 at `0x1F801050` and not a generic
RAM-backed serial structure.

## Exact handshake

The routine first performs:

```c
I_STAT = ~0x80u;
```

PlayStation `I_STAT` uses write-zero-to-acknowledge semantics. Writing all ones
except bit 7 therefore acknowledges only the SIO interrupt source while
preserving other pending interrupt flags.

It then tests `JOY_STAT & 0x80`. If already clear, it skips the wait. Otherwise
it loops in this exact order:

```c
while (JOY_STAT & 0x80) {
    if (HasTimer2TimeoutExpired())
        return false;
}
```

The loop's actual first action is the timeout poll after the initial status
test; subsequent iterations reread status, then return to the timeout poll.
The helper does not arm Timer 2 itself. Every caller does so immediately before
entry through `ArmTimer2Timeout`, commonly with threshold `0x3C` and once with
`0x7D0`.

On success it performs a read/modify/write:

```c
JOY_CTRL = JOY_CTRL | 0x10;
return true;
```

Bit `0x10` is the JOY acknowledge/IRQ-reset control bit. The timeout path does
not write JOY_CTRL; the initial I_STAT acknowledgement has already occurred.

## State and side effects

The complete effects are limited to:

- one `I_STAT` write acknowledging interrupt bit 7;
- repeated JOY_STAT reads while necessary;
- Timer 2/shared-deadline reads delegated to `HasTimer2TimeoutExpired`;
- one JOY_CTRL bit-set write on success only;
- the boolean return.

It does not transmit or receive a JOY data byte, arm/reset Timer 2, change RAM
queue pointers, invoke a callback, or retry a higher-level controller command.

## Complete caller and interior-target census

An exhaustive aligned instruction-word scan of the retained executable finds
exactly seven direct calls:

```text
80169648 801696C4 80169740 8016983C
8016C938 8016CABC 8016CB68
```

All seven target `0x80169D30`; there are no direct `j` or `jal` targets to any
interior address in `[0x80169D34,0x80169DC0)`.

Callers arm Timer 2 immediately before entry, branch on the boolean, and route
zero to their controller/SIO timeout handling (including `-3` in the multitap
path). Successful low-level transfer callers proceed to `0x80169DC0`, which
waits for JOY_STAT receive-ready bit 1. This sequencing independently confirms
that the audited owner is the bounded ACK/IRQ phase, not the data receive wait.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, initialized register pointers, callers, and aligned targets;
- `generated/SCES_028.45_full.ranges` for the exact `0x90` owner boundary;
- `docs/audits/semantic-8016c53c.md` for the independently proven shared Timer
  2 expiration predicate;
- controller/multitap caller windows for deadline arming, boolean consumption,
  and timeout propagation.
