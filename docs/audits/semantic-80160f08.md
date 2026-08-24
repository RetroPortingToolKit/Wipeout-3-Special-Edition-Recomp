# Semantic audit: `0x80160F08` / `CdReadyCallback`

## Result and ABI

`0x80160F08` is the canonical LibCD ready-callback exchange routine:

```c
typedef void (*CdlCB)(uint8_t phase, uint8_t *result);
CdlCB CdReadyCallback(CdlCB callback);
```

It returns the previous callback from word `0x8017B128` and installs incoming
`a0`.  Null is a valid value used to suppress ready-event dispatch.

## Exact boundary and target census

The generated range assigns `[0x80160F08, 0x80160F1C)`, exactly `0x14` bytes,
to this frameless five-instruction leaf.  It loads the old callback, returns at
`0x80160F14`, and stores the replacement in the return delay slot at
`0x80160F18`.  The larger `CdControl` owner begins independently at
`0x80160F1C`.

An aligned scan of the authenticated executable finds exactly nine direct JAL
sites:

```text
80116294 801381A8 80160734 80160818 801608AC
80160A04 80160B8C 80163CBC 80163D54
```

There are no calls to any interior instruction at `0x80160F0C..0x80160F18`.

## State behavior

The exact exchange is:

```c
CdlCB old = *(CdlCB *)0x8017B128;
*(CdlCB *)0x8017B128 = callback;
return old;
```

The old-value load precedes the delay-slot store.  Incoming values are stored
verbatim with no null special branch, alignment check, address validation, or
callback invocation.  The routine performs no critical section, interrupt
masking, memory barrier, nested-save stack, or reference count.  Callers that
temporarily suppress the callback must retain and restore the returned pointer
themselves.

The lower CD command/IRQ core distinguishes this word from the adjacent sync
callback: ready event bit `2^2` dispatches `0x8017B128`, whereas sync event bit
`2^1` dispatches `0x8017B124`.  The ready callback receives the event phase
byte and the ready-response buffer, establishing the `CdlCB` signature.

## Caller roles

The caller set includes null suppression during game/CD lifecycle transitions,
restoration from saved callback fields, and installation of concrete LibCD
handlers including `0x80160548` and `0x80163D7C`.  Several owners exchange both
ready and sync callbacks as a pair, but the two globals remain independent:
calling `CdReadyCallback` never reads or writes `0x8017B124`.

## Confidence

`confirmed`: canonical identity, exact boundary, function-pointer ABI and
old-value return, exhaustive caller/interior-target census, ready-vs-sync
global identity, dispatch signature, null suppression, and absence of
validation/synchronization are instruction-proven.
