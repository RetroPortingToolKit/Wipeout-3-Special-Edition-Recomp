# Semantic audit: `0x80134AD8` / `IsRaceRecordStatusZeroTypeNonzero`

## Result

`0x80134AD8` is a pure racer-record predicate now named
`IsRaceRecordStatusZeroTypeNonzero`:

```c
bool IsRaceRecordStatusZeroTypeNonzero(int racer_index);
```

It rejects negative indices and otherwise returns true exactly when the
selected `0x3A4`-byte race record has byte `+0 == 0` and byte `+3 != 0`.

## Exact owner and ABI

The generated interval `[0x80134AD8,0x80134B60)` is exact, length `0x88`. It
is a frameless leaf with a shared `jr ra` / `nop` at
`0x80134B58..0x80134B5C`. The next independent owner begins at `0x80134B60`.
Generated entries at `0x80134AE4`, `0x80134B18`, `0x80134B20`,
`0x80134B54`, and `0x80134B58` are internal branches and the shared return,
not callable aliases.

Input `a0` is treated as signed for the initial negative test. Output `v0` is
normalized to zero or one. The helper performs no stores, calls, global
mutation, or hardware access.

## Exact predicate

For nonnegative `i`, the shift/add sequence is exactly:

```text
record = 0x801910E0 + i * 0x3A4
```

The first unsigned-byte load and `sltiu value,1` accept only record byte `+0`
equal to zero. Only then does the routine index the same record again and load
byte `+3`; zero returns false and every nonzero value returns true:

```c
if (i < 0)
    return false;
return g_race_records[i].byte_0 == 0 &&
       g_race_records[i].byte_3 != 0;
```

There is no upper-bound check. Callers must supply a negative sentinel or a
valid record index.

The nearby audited `IsRaceRecordStatusZeroTypeOne` at `0x80134964` has the
same stride and first-byte gate but requires signed byte `+3 == 1`. This helper
is intentionally named `TypeNonzero`: values 2 through 255 pass here even
though they fail the stricter predicate. The field's original enum name is not
claimed.

## Complete caller and interior-target census

An exhaustive aligned scan finds exactly six direct calls, all to the entry:

```text
800EB0AC 8011A960 8011A9A0 8011BC64 8011C470 8011C510
```

Every caller obtains the index from byte `+0x15A` of a racer-owned or
racer-associated object and branches on the Boolean result. The call sites
gate physics/position adjustment, timer bookkeeping, and several sound/effect
paths; none relies on an incidental register value or a narrower equality-to-1
contract. No external direct jump or JAL targets an interior instruction.

The caller distribution and the independently established `0x3A4` record
base/stride support the racer-record domain. More specific meanings for bytes
`+0` and `+3` would exceed the available producer evidence, so the name keeps
their proven status/type roles explicit.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact
  disassembly, all caller windows, and exhaustive target scans;
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_full_39.c` for the exact owner and internal blocks;
- `docs/audits/semantic-80134964.md` and the race-viewport initialization
  audit for the adjacent predicate and independent record-stride evidence.
