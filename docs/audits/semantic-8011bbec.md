# Semantic audit: `0x8011BBEC` / `UpdateRacerContactResponseHandler`

## Result and boundary

`0x8011BBEC` conditionally changes the active response callback for a racer and
keeps two companion flags on its associated craft/object synchronized. The
conservative name is `UpdateRacerContactResponseHandler`:

```c
void UpdateRacerContactResponseHandler(int racer_index,
                                       const ContactRecord *contact);
```

The exact owner is `[0x8011BBEC,0x8011BD4C)`, length `0x160`. It creates a
32-byte frame, saves `ra` and `s0..s2`, and returns through
`0x8011BD34..0x8011BD48`. The next owner begins independently at
`0x8011BD4C`. No external target enters the body.

## Inputs and admission gates

`a0` indexes the established `0x3A4`-byte racer records at `0x801910E0`.
Racer byte `+1` then selects a `0x1AC`-byte craft/object from the array rooted
at `*(void **)0x801D7038`. `a1` is read only at word `+4`; bit `0x1000` must be
set or the routine returns unchanged.

Further rejection occurs when:

- the selected craft/object's racer index at `+0x15A` satisfies
  `IsRaceRecordStatusZeroTypeNonzero`;
- global word `0x80175EB0` is nonzero; or
- the current callback at racer offset `+0x39C` is `0x800E9510`.

There are no explicit index, pointer, or bounds checks beyond these semantic
gates, and no scalar return value is consumed by callers.

## Exact transition table

The decision uses craft/object flags at `+0xE4`, specifically mask
`0x00010004`, plus global `0x8018AE94`:

| Condition | Callback stored at racer `+0x39C` | Final object bits |
|---|---|---|
| masked flags equal `0x00000004` and global is zero | `0x800E913C` | bit `0x4` set, bit `0x10000` clear |
| masked flags equal `0x00000004` and global nonzero, or equal `0x00010004` | `0x800E8E14` | both bits cleared |
| every other masked value | `0x800EA760` | bit `0x4` set, bit `0x10000` clear |

These installed functions are substantial craft/racer physics-response owners:
they resolve the same `0x1AC`-stride object and perform orientation/vector and
motion work. The excluded `0x800E9510` callback is another owner in that same
response family. This supports the contact-response description without
claiming a narrower collision, weapon, or track-feature identity not proven by
the contact producer.

## Callers, interior targets, and overlay accounting

Exactly three executable calls target the entry:

```text
800E469C 8011988C 8011A854
```

The first is in the main craft physics/contact path and passes its current
interaction record. The other two occur in object-event update paths adjacent
to related contact/effect helpers. All pass a racer index and the same
record-shaped second argument.

The only direct interior jumps are the owner's own joins at `0x8011BD30` and
`0x8011BD34`; no external J/JAL targets an interior address. A full exact-body
scan finds only `0x8011BBEC`, so overlay overlap produces neither a duplicate
implementation nor duplicate logical callers.

## Sources checked

- `disc/SCES_028.45`, excluding its `0x800` header, for exact instructions,
  callers, interior targets, and duplicate-body scan;
- `generated/SCES_028.45_full.ranges` for the exact owner;
- the callback bodies at `0x800E8E14`, `0x800E913C`, `0x800E9510`, and
  `0x800EA760`, plus the independent racer predicate audit.
