# Semantic audit: `0x800E8350` / `ApplyRgbToPrimitiveRecordStream`

## Result

`0x800E8350` is a frameless in-place primitive color applicator now named
`ApplyRgbToPrimitiveRecordStream`:

```c
void ApplyRgbToPrimitiveRecordStream(PrimitiveOwner *owner,
                                     const uint8_t rgb[3]);
```

It walks `owner->record_count` typed, variable-size primitive records and
replaces every applicable vertex RGB triplet with the supplied color. Two
record families receive the color at full intensity and two receive each
unsigned channel divided by two.

## Exact owner and ABI

The generated range table gives `0x800E8350` a `0x1C4`-byte owner. The six
additional generated entries at `0x800E83A8`, `0x800E83C0`, `0x800E83E4`,
`0x800E8424`, `0x800E8464`, and `0x800E84B0` all repeat that same owner range:
they are jump-table cases, not functions. The common return is
`0x800E850C..0x800E8510`; the next independent owner begins at `0x800E8514`.
Thus the exact callable is `[0x800E8350,0x800E8514)`.

`a0` supplies an owner whose signed halfword at `+0x12` is the record count and
whose word at `+0x1C` points to the first record. `a1` supplies three unsigned
bytes in RGB order. A non-positive count returns immediately. The routine has
no frame and makes no calls. It establishes no meaningful scalar result, every
caller ignores register residue, and its only state effect is writing RGB bytes
in the pointed record stream.

## Type table and exact writes

The eight words at jump table `0x8016CDE0` map raw record types as follows:

| Type | Case | Record size | RGB writes |
| --- | --- | ---: | --- |
| 1 | `0x800E83A8` | `0x0C` | full RGB at `+0x08` |
| 2 | `0x800E83C0` | `0x18` | half RGB at `+0x14` |
| 3 | `0x800E83A8` | `0x0C` | full RGB at `+0x08` |
| 4 | `0x800E83C0` | `0x18` | half RGB at `+0x14` |
| 5 | `0x800E83E4` | `0x14` | full RGB at `+0x08,+0x0C,+0x10` |
| 6 | `0x800E8464` | `0x1C` | half RGB at `+0x10,+0x14,+0x18` |
| 7 | `0x800E8424` | `0x18` | full RGB at `+0x08,+0x0C,+0x10,+0x14` |
| 8 | `0x800E84B0` | `0x24` | half RGB at `+0x14,+0x18,+0x1C,+0x20` |

“Half” is exactly `channel >> 1` after an unsigned byte load, independently
for R, G, and B; there is no rounding or saturation. Multi-vertex colors are
triplets in four-byte vertex-color fields, so the fourth/control byte remains
untouched. A type outside 1..8 takes the default path, performs no write and
does not advance the stream pointer, but the finite record ordinal still
advances. Valid streams therefore rely on the proven 1..8 type set.

The paired type mappings (1/3 and 2/4) and the 3- versus 4-vertex cases explain
the shared case targets without inventing unproven primitive enum names.

## Exhaustive callers and overlay duplication

An aligned scan of the retained executable finds exactly four unique JALs to
the entry:

```text
800E6E6C  800E7188  800E77A8  800E77F0
```

No J/JAL targets an interior address. The only interior entries are the six
indirect jump-table targets above and the five direct case exits to the common
loop tail at `0x800E84F8`.

Generated output emits seven calls because overlapping overlay translations
repeat the executable words at `0x800E6E6C`, `0x800E77A8`, and `0x800E77F0`;
these are duplicates, not additional callers. At the first two unique sites,
`a1` is `0x80173228`, while the latter two index four-byte color entries using
object bytes `+0x8D` and `+0x8E`. All four pass a primitive owner in `a0`, and
none reads `v0` after return, matching the proven mutating void contract.

## Sources checked

- `disc/SCES_028.45`, accounting for its `0x800`-byte PS-X EXE header, for
  exact instructions, jump-table words, unique callers, and target scans;
- `generated/SCES_028.45_full.ranges` for the owner and false interior entries;
- generated call sites for argument provenance and emitted-overlay duplicates.
