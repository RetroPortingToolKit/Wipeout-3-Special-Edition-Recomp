# Semantic audit: `0x8013491C`

## Result

The callable at `0x8013491C` is named `IsRacerSlotVacant`. It is an exact
`0x48`-byte leaf spanning `0x8013491C..0x80134963`. Its recovered prototype is
equivalent to:

```c
bool IsRacerSlotVacant(int index)
{
    if (index < 0)
        return false;
    return *(uint8_t *)(0x801910E0 + index * 0x3A4) == 0;
}
```

The neighboring predicate at `0x80134964` independently establishes the same
base and stride as the racer table. "Vacant" describes the proven leading-byte
test without claiming what other states that byte may encode.

## Boundary and ABI evidence

- `0x8013491C` immediately tests signed `a0`; no state from the preceding
  routine is consumed.
- The nonnegative path computes the index stride as
  `((((index * 8 - index) * 4 + index) * 8 + index) * 4)`, exactly
  `index * 932` (`0x3A4`), and adds base `0x801910E0`.
- It reads only the unsigned byte at record offset `+0x000`.
- The branch at `0x80134950` makes `v0 = 1` only when that byte is zero. Both a
  nonzero byte and every negative index produce `v0 = 0`.
- `0x8013495C` is `jr ra`, with a NOP delay slot at `0x80134960`. The next
  callable begins independently at `0x80134964` and first copies its own `a0`.
  Thus `0x48`, rather than a generated split or an inferred tail, is the exact
  function extent.

## Complete direct-caller census

Scanning every aligned instruction word in the retained PS-X EXE payload for
the unique JAL encoding `0x0C04D247` finds exactly 81 direct calls:

```text
800E2E1C 800E483C 800E48A4 800EB5D0 800EB5E0 800EB640 800EB6A4
800EBB30 800EBB40 800EFF3C 800F2150 800F2748 800F7F80 800F9F38
800FE224 800FE410 800FE6CC 80102418 80102534 80104774 801047A0
801069D0 8010DB20 8010DB9C 8010DC88 8010DD04 8010DDC4 8010E188
8011DE34 8011DF48 80123AE0 80123B5C 801276B4 8012771C 8012777C
80127C68 80127EE4 801309C8 80133264 80133970 80133F98 80134390
801391BC 8013CC00 801455F8 80145D5C 801463F8 80146A08 8014EB78
8014EC00 8014EEC8 8014EF0C 8014EF50 8014EF68 8014F188 8014F314
8014F4EC 8014F694 80150594 80150AFC 80151194 80151BF0 80151DB4
8015223C 80153E0C 80153F14 80154A30 80154EA0 8015594C 80155E90
8015670C 80156770 80156814 80156850 801569B8 801586DC 80159374
80159560 80159B6C 8015A5FC 8015AB68
```

This census agrees with the generated AOT call sites and includes the call at
`0x80133F98` in the final mapping pass of `InitializeRaceViewportState`.
There are no aligned JALs to an interior instruction of the predicate.

## Sources checked

- `disc/SCES_028.45`: authoritative instruction bytes and direct-call census.
- `generated/SCES_028.45_full.ranges`: exact generated entry/range (`0x48`).
- `generated/SCES_028.45_full_121.c`: all four basic blocks, stride arithmetic,
  byte load, predicate construction, return, and adjacent next entry.
- `generated/SCES_028.45_full_100.c` through `_132.c`: generated direct-call
  sites across the retained main executable.
- `docs/audits/semantic-80133d1c-owner.md`: independently recovered owner of
  the `0x80133F98` call and the matching `0x3A4` race-record stride.
