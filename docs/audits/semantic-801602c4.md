# Semantic audit: `0x801602C4` / `CdGetToc`

## Result and ABI

`0x801602C4` is the LibCD `CdGetToc` wrapper, not another A0 BIOS trampoline:

```c
int CdGetToc(CdlLOC *toc);
```

It forwards the caller's output pointer as the second argument to the TOC
worker at `0x801602E8`, installs constant `1` as the worker's first/session
argument, and returns the worker's `v0` unchanged.

## Exact boundary and incoming targets

The generated range assigns `[0x801602C4, 0x801602E8)`, exactly `0x24` bytes,
to this owner.  It opens a 24-byte frame, saves only `ra`, calls the worker,
restores its frame, returns at `0x801602E0`, and executes a NOP return delay
slot at `0x801602E4`.  The worker begins independently at `0x801602E8`.

An aligned scan of the authenticated executable finds exactly one direct
entry JAL, at `0x80137F08`, and no calls to an interior instruction in
`[0x801602C8, 0x801602E8)`.  The wrapper itself has exactly one outgoing JAL,
to `0x801602E8` at `0x801602D0`.

## Forwarding formula and return

The complete wrapper logic is:

```c
int CdGetToc(CdlLOC *toc) {
    return ReadCdTocWorker(1, toc);
}
```

At machine level it first copies incoming `a0` to `a1`, then sets `a0 = 1` in
the worker JAL delay slot.  Nothing after the call writes `v0`, so success and
failure results propagate exactly.  The current worker binary immediately
uses `a1` as its output base and does not otherwise consume the selector, but
the wrapper still establishes the canonical two-argument internal call shape.

## TOC identity and effects

The callee establishes the identity independently of the wrapper's shape.  It
issues CD command `0x13` (`GetTN`) to obtain the first/last BCD track numbers,
then command `0x14` (`GetTD`) for the lead-out and each track.  It converts the
BCD range to integers and writes four-byte `CdlLOC`-style entries through the
forwarded pointer.  It returns the populated track count on success and zero
when a CD command fails, restoring the prior CD callback/state around the
operation.

The sole game caller passes a retained TOC buffer, stores the returned count at
`0x8018F094`, and applies a game-side clamp afterward.  Thus the result is
observably consumed, unlike incidental register values in a void SDK stub.

The 0x24-byte wrapper itself performs no global or device access; its direct
state effects are its stack-frame save/restore.  CD commands, output writes,
logging, callback preservation, and failure handling belong to the worker.

## Confidence

`confirmed`: exact wrapper boundary, one-argument ABI, constant internal
selector, return propagation, sole executable caller, zero interior targets,
and canonical TOC identity from the worker's GetTN/GetTD command sequence and
caller consumption are instruction-proven.
