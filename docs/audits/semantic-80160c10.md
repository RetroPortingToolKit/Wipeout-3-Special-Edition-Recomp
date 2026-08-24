# Semantic audit: `0x80160C10` / `CdReadSync`

## Result

`0x80160C10` is the PsyQ-compatible asynchronous CD-read synchronizer now
named `CdReadSync`:

```c
int CdReadSync(int mode, uint8_t *result);
```

It is paired with the adjacent three-argument asynchronous `CdRead` owner at
`0x80160A74`: game callers start a read there and then either poll this routine
with mode 1 or call it once with mode 0 to wait.

## Exact owner and ABI

The generated range table assigns exactly `0x104` bytes to `0x80160C10`. The
function has a conventional `0x28`-byte frame, returns through
`0x80160CF4..0x80160D10`, and the next independent leaf begins at `0x80160D14`.
Its exact extent is therefore `[0x80160C10,0x80160D14)` with no generated split
or overlay duplicate.

`a0` is the synchronization mode: zero selects the internal wait loop and any
nonzero value performs one polling pass. `a1` is forwarded as the optional
result/status buffer to the command-status service at `0x80160ED4`. The signed
return is the current read-progress/error value, with one completion condition
normalized from zero to one as described below.

## Read state, deadlines, and behavior

The routine uses the CD-read state block surrounding `0x80179288`:

```text
80179288  recovery/restart result
8017929C  current read-progress value
801792A0  short-deadline baseline
801792A4  long-deadline baseline
801792AC  completion/status flag
```

It samples `VSync(-1)` and applies signed comparisons exactly as follows:

- if `long_baseline + 1200 < now`, select `-1` immediately;
- otherwise, if progress is negative or `short_baseline + 60 < now`, invoke
  `0x8016088C(1)` and select the recovery result at `0x80179288`;
- otherwise select the current progress value at `0x8017929C`.

In mode zero, it repeats from the VSync sample while the selected state and
completion flag indicate the asynchronous operation is still active. The
literal loop conditions are: with flag zero, repeat for positive state; with
flag nonzero, repeat for zero state. Negative values leave the loop.

Before returning, every path invokes `0x80160ED4(1, result)`, the wrapper around
the lower CD command-status synchronizer. If the completion flag is nonzero and
the selected state was zero, the return is normalized to one; otherwise the
selected signed state is returned unchanged. The function itself writes no CD
state globals, but its recovery and status callees may update the shared state
and optional result buffer.

## Identity and callers

An exhaustive aligned scan of the retained executable finds six unique JALs,
all to the true entry and none to an interior address:

```text
80112D78  80112E60  8011315C  801132B0  80113738  8016477C
```

The first five sites immediately follow `CdRead(..., 0x80)`, call
`CdReadSync(1, NULL)`, and loop while the signed return is positive. The sixth
also immediately follows `CdRead(..., 0x80)` but calls `CdReadSync(0, NULL)`
once and converts a zero return to boolean success. This independently proves
the poll-versus-wait mode contract, progress return, optional result pointer,
and canonical SDK identity.

No emitted call-site duplication is present: the generated corpus also has six
calls. Within the owner, all transfers are ordinary local branches except the
single direct loop-back jump `0x80160C9C -> 0x80160CA8`; no external instruction
targets any of them.

## Sources checked

- `disc/SCES_028.45`, accounting for its `0x800`-byte PS-X EXE header, for
  exact instructions, unique callers, arguments, and interior-target scans;
- `generated/SCES_028.45_full.ranges` for the exact owner boundary;
- generated call sites for the six-call emission count and return consumers;
- adjacent `0x80160A74` read setup and `0x80160ED4` status wrapper for the
  asynchronous CD-read state-machine relationship.
