# Semantic audit: `0x80160EF4` / `CdSyncCallback`

## Result and ABI

`0x80160EF4` is the canonical LibCD sync-callback exchange routine:

```c
typedef void (*CdlCB)(uint8_t phase, uint8_t *result);
CdlCB CdSyncCallback(CdlCB callback);
```

It returns the callback previously installed in word `0x8017B124` and replaces
that word with incoming `a0`.  Passing null therefore suppresses sync-callback
dispatch while preserving the old pointer for a later restore.

## Exact boundary and target census

The generated range assigns `[0x80160EF4, 0x80160F08)`, exactly `0x14` bytes,
to this frameless five-instruction leaf.  It loads the old callback, returns at
`0x80160F00`, and performs the replacement store in the `jr ra` delay slot at
`0x80160F04`.  The adjacent callback exchange at `0x80160F08` is a separate
owner.

An aligned scan of the authenticated executable finds exactly twelve direct
JAL sites:

```text
8011629C 801162AC 801381B8
80160318 801604A8 801604D8
8016052C 80160764 80160848
801608A4 80160B2C 80160B80
```

There are no JALs to `0x80160EF8`, `0x80160EFC`, `0x80160F00`, or
`0x80160F04`.

## Exchange and ordering semantics

The complete state operation is:

```c
CdlCB old = *(CdlCB *)0x8017B124;
*(CdlCB *)0x8017B124 = callback;
return old;
```

The load necessarily precedes the delay-slot store, so passing the currently
installed callback returns and reinstalls the same value.  The incoming
pointer is stored verbatim: null, unaligned, and otherwise invalid values are
not rejected.  There is no interrupt masking, critical-section call, memory
barrier, reference count, or nested-stack management.  Correct save/restore
nesting is entirely a caller responsibility.

The lower CD command core reads `0x8017B124` when dispatching a sync completion
and calls it with the phase byte plus internal response buffer.  This consumer
establishes the `CdlCB` role; the exchange leaf itself never invokes the
callback.

## Caller roles

The twelve sites cover all three intended patterns:

- install or restore a game/LibCD callback pointer, including callback
  `0x80160514` in the streaming paths;
- pass zero and save the returned pointer while a synchronous operation runs;
- pass the saved pointer on both success and failure exits.

`CdGetToc2` accounts for the suppress/restore sites at `0x80160318`,
`0x801604A8`, and `0x801604D8`.  Other LibCD owners use the same exchange for
streaming and lifecycle transitions.  No caller relies on a scalar other than
the returned old callback pointer.

## Confidence

`confirmed`: canonical identity, exact boundary, function-pointer ABI and old
value return, exhaustive caller/interior-target census, load-before-store
ordering, null suppression, callback consumer signature, and absence of
validation/synchronization are instruction-proven.
