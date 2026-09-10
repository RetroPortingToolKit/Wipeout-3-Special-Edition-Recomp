# Semantic audit: `0x801602E8` / `CdGetToc2`

## Result and internal ABI

`0x801602E8` is the LibCD TOC worker whose retained diagnostic strings name it
`CdGetToc2`:

```c
int CdGetToc2(int selector, CdlLOC *toc);
```

Both executable callers pass `selector = 1`, but the compiled body overwrites
`a0` before reading it and therefore ignores that argument.  It preserves `a1`
as the output base.  The return is the number of populated track records on
success and zero on any command failure or an empty/reversed track range.

## Exact boundary and incoming targets

The generated range assigns `[0x801602E8, 0x8016050C)`, exactly `0x224` bytes,
to this owner.  It opens a 64-byte frame, saves `ra` and `s0..s6`, joins at the
restore block beginning `0x801604E4`, returns at `0x80160504`, and restores
`sp` in the delay slot at `0x80160508`.  The next owner begins at
`0x8016050C`.

An aligned scan of the authenticated executable finds exactly two entry JALs:

```text
80112CD0  801602D0
```

The second is the public `CdGetToc` wrapper; the first is a game-side caller
that invokes the worker directly with the same selector value.  There are no
direct JALs to any interior instruction in `[0x801602EC, 0x8016050C)`.

## Callback isolation and command sequence

At entry the worker calls the callback setter at `0x80160EF4` with zero,
saving its returned prior value from global sync-callback word `0x8017B124`.
Every success and failure exit calls the same setter with that saved value, so
the incoming CD sync callback is restored exactly once after the TOC operation.

Commands use the synchronous `CdControlWithRetry` interface at `0x8016118C`:

1. command `0x13` (`GetTN`), with no parameter buffer and an eight-byte local
   response buffer, obtains first and last track bytes;
2. command `0x14` (`GetTD`) with parameter byte zero obtains the lead-out;
3. command `0x14` repeats once for every track from decoded first through last,
   with the requested track encoded as packed BCD.

Every command is tested immediately.  A zero result branches to the common
failure path; no later track is requested after the first failure.

## BCD range and TOC layout

The GetTN payload bytes are converted independently from packed BCD using
`10 * high_nibble + low_nibble`.  Let these integer values be `first` and
`last`.  The output uses four-byte stride:

```text
toc + 0:  lead-out minute, lead-out second, 0, untouched byte
toc + 4:  first-track minute, first-track second, 0, untouched byte
toc + 8:  next-track minute, next-track second, 0, untouched byte
...
```

The minute and second bytes come from response offsets `+1` and `+2`; they are
copied without BCD conversion.  Byte `+2` of each destination record is
explicitly zeroed, while byte `+3` is never written.  The lead-out is always
written before the track loop after successful GetTD(0), so a later track
failure leaves that record and any earlier track records partially published.
There is no rollback or output-capacity argument.

For each integer track `n`, the request byte is computed as
`((n / 10) << 4) + (n % 10)`.  The loop is inclusive through `last`.  Its
record counter begins at one for the lead-out slot and the function returns
`counter - 1`, which is `last - first + 1` for a valid range.  If
`last < first`, it writes the lead-out but returns zero without issuing a track
request.

## Failure and diagnostics

On any GetTN/GetTD failure, the worker optionally prints retained string
`CdGetToc2: error` when CD debug level `0x8017B12C` is nonzero, restores the
saved sync callback, and returns zero.  It does not clear output already
written.

At debug level at least two it prints the decoded first/last range and each
TOC minute/second pair using retained strings `track=%d,%d` and
`CdGetToc2: %02x:%02x:00`.  Diagnostic output does not change the return or
record layout.

## Confidence

`confirmed`: retained identity, exact boundary, effective ABI/return,
exhaustive caller/interior-target census, callback preservation, GetTN/GetTD
ordering, BCD formulas, four-byte output stride and untouched byte, inclusive
loop/count, partial-output failures, and debug gates are instruction-proven.
