# Semantic audit: `0x8016118C`

## Result and boundary

`0x8016118C` is the synchronous CD-ROM command owner
`CdControlWithRetry` (`known`).  The exact generated range is
`R 8016118C 14C`, so it occupies 332 bytes at
`0x8016118C..0x801612D7`.  It creates a 56-byte frame, preserves
`ra`, `fp`, and `s0..s7`, and returns through `0x801612D0/0x801612D4`.
The independent callable at `0x801612D8` is not part of this routine, and no
interior address is a direct-call target.

## Calling-register protocol

The instruction stream proves this effective interface:

```c
int CdControlWithRetry(
    uint32_t command,  // a0; only low 8 bits are retained
    void *tx,          // a1; optional source/request buffer
    void *rx);         // a2; destination/response buffer
```

`a0` is masked with `0xff` into `s3`; `a1` and `a2` are preserved in `s1` and
`s2`.  Every real transfer invokes the lower CD command core at `0x80168080` with
`a3 = 0`.  That callee receives one of three command values:

- `(1, NULL, NULL, 0)` for a conditional reset/synchronization exchange;
- `(2, tx, rx, 0)` for an optional preflight exchange;
- `(command & 0xff, tx, rx, 0)` for the requested transaction.

This owner does not access the `0x1F801800..0x1F801803` CD-ROM register block
directly; register writes, IRQ-phase polling, and response movement are
delegated to `0x80168080`.
Accordingly, `tx` and `rx` are opaque buffers here.  The routine never reads
or writes their contents itself, though the recovery classifier later receives
`rx`.

## Global protocol state and retries

At entry, the routine saves the word at `0x8017B124`.  Before every attempt it
writes zero there, suppressing the installed callback/hook while the synchronous
exchange is active.  It restores the saved word before invoking the requested
command and also restores it after retry exhaustion.  Thus every normal exit
leaves the prior callback word intact.

For commands other than `1`, status byte `0x8017B130` is tested for bit `0x10`.
When set, the routine issues the command-1 reset/synchronization exchange before
continuing.  If `tx != NULL`, a command-indexed word is loaded from the table at
`0x801792D8 + 4*(command & 0xff)`; a nonzero table entry enables the command-2
preflight exchange.  A nonzero preflight result rejects that attempt before the
requested command is sent.

The retry counter starts at `3`.  Each failed attempt decrements it and loops
while it is not `-1`, proving at most four attempts.  There is no independent
clock read in this owner: its timeout semantics are a bounded attempt budget,
while any per-command/IRQ-phase timeout is reported by `0x80168080` as a
nonzero result.  A zero result from the requested exchange returns success
immediately.

## Error and return semantics

If all attempts fail, the local result is set to `-1` and the saved callback
word is restored.  The common error path then calls `0x80167B38(0, rx)`.
The final scalar is exactly `(recovery_result == 2)`: one when that recovery
classifier returns `2`, otherwise zero.  Therefore the externally visible
failure return is a normalized recovery/status predicate, not the raw
`0x80168080` error code and not the internal `-1` retry sentinel.

This distinction matters for callers: nonzero low-level results control retry,
but only the recovery classifier's value `2` survives as return value `1`.
Successful requested transactions return `0` without calling the recovery
classifier.

## Callers

There are 14 direct call sites:

- `0x80118DFC`, inside `0x80118588`;
- `0x80137C48`, `0x80137CFC`, `0x80137D84`, `0x80137DA0`, and
  `0x80137DB0`, all inside `0x80137BE4`;
- `0x80137F00`, inside `0x80137ED8`;
- `0x8013813C` and `0x8013815C`, inside `0x801380CC`;
- `0x80138198`, inside `0x80138188`;
- `0x80160330`, `0x801603AC`, and `0x80160424`, inside `0x801602E8`;
- `0x80160BDC`, inside `0x80160A74`.

The callers span game-facing and CD-support owners, consistent with a common
CD command adapter rather than a single command implementation.

## Confidence limits

The command byte, buffer forwarding, callback suppression, status-bit gate,
four-attempt bound, and exact return normalization are instruction-level facts.
The semantic names “reset/synchronization” and “preflight” describe their
observed ordering roles; the original device-specific command names and buffer
record layouts require audits of `0x80168080`, its callers, and the producers of
the table at `0x801792D8`.
