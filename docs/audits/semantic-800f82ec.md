# Semantic audit: `0x800F82EC` / `HandleErrorSeverity`

## Result and ABI

`0x800F82EC` is the shared error-policy adapter.  In this executable the
diagnostic text is retained at call sites but is not displayed by the helper;
severity `2` terminates through the PlayStation BIOS, and every other severity
returns.  A conservative descriptive ABI is:

```c
void HandleErrorSeverity(const char *diagnostic, int16_t severity);
```

The function never reads `a0`.  It explicitly normalizes `a1` by shifting left
16 and arithmetically shifting right 16.  No caller consumes `v0`, and normal
paths leave comparison intermediates rather than a semantic return value.
`HandleErrorSeverity` is descriptive: the executable contains no symbol table
that proves a retained source-level function name.

## Exact boundary and incoming targets

The generated range assigns `[0x800F82EC, 0x800F832C)`, exactly `0x40` bytes,
to this owner.  It opens a 24-byte frame at `0x800F82EC`, saves only `ra`, joins
at the restore block `0x800F831C`, returns at `0x800F8324`, and restores `sp`
in the delay slot at `0x800F8328`.  The next owner begins at `0x800F832C`.

An aligned scan of the authenticated executable finds exactly 31 entry JALs:

```text
800E6BD8 800E7074 800E7DCC 800E7F58 800E830C 800F048C
80111468 80111960 801123F8 80115B60 80115FAC 80117A30
80117A5C 8012CB6C 8012CC60 8012CC9C 8012CDC4 8012CE24
8012CE6C 8012CE88 8012D990 8012E440 8012E48C 80130CA8
80131434 801317FC 80140160 80141BE4 8014470C 8014B8D4
8014B918
```

There are no direct JALs to an interior instruction in
`[0x800F82F0, 0x800F832C)`.  Every call site sets `a1 = 2` in the JAL delay
slot.  `a0` is a static diagnostic pointer or a formatted buffer produced just
before the call.  Representative retained text includes `Bad primitive type`,
`Failed to allocate memory`, `File %s not found`, `Maximum number of objects
exceeded`, and `New Instance could not allocate free memory`.

## Severity and control effects

The branch sequence is exact but redundant:

1. negative signed-16 severity returns;
2. severity less than `2` returns;
3. severity greater than or otherwise unequal to `2` returns;
4. only severity exactly `2` calls `0x8015FD7C` with `a0 = 0`.

`0x8015FD7C` loads BIOS call selector `t1 = 0x38`, jumps to the B0 vector at
`0x000000B0`, and is the standard BIOS `exit` adapter.  Thus the argument is
exit status zero.  The helper does not itself print, copy, store, or format the
diagnostic and has no global-memory, GPU, GTE, CD, or audio side effect.

Under the original BIOS contract the severity-2 path does not return to the
caller.  The ordinary restore block remains reachable only for nonfatal
severity values (or if a replacement BIOS were to violate the exit contract).
Because all 31 executable callers pass `2`, every observed use is fatal even
though the adapter's general interface accepts nonfatal values.

## Confidence

`confirmed`: exact boundary, signed-16 ABI normalization, ignored first
argument, exhaustive caller/interior-target scan, all caller severities,
severity predicate, BIOS vector/selector and zero exit status, absence of
diagnostic output/state mutation, and normal-path return behavior are
instruction-proven.  A historical source spelling is intentionally not
claimed.
