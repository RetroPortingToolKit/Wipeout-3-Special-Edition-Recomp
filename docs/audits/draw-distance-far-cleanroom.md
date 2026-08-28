# Draw Distance selector cleanroom map

Date: 2026-08-28

## Result

Hueponik's complete `DRAW DIST` behavior can be applied to the authenticated stock
SCES-02845 title without installing the patched executable, its code island,
or its high-RAM guest engine. The direct implementation preserves the stock
dual-view scene-mask owner and applies the selected result at the owner's
single return site.

The authenticated comparison inputs are:

- stock executable SHA-256
  `0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d`;
- Hueponik PAL executable SHA-256
  `7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac`;
- stock owner `0x801420C4..0x801422FC`, classified independently as the
  four-argument dual-view scene-mask configurator; and
- captured 8 MiB race states in which Hueponik's generated visibility lists
  and selected FAR profile are resident.

No bytes from the patched executable are proposed for reuse.

## Recovered behavior

Hueponik redirects stock owner entry `0x801420C4` to guest code at
`0x80781044`. That code precomputes three packed visibility lists per track
section in high RAM, then resumes the unmodified stock owner at `0x801420CC`.

For each current 64-byte track-section record, it derives a normalized planar
heading from that section to the next section. For every candidate section it
computes:

```text
longitudinal = abs(deltaZ * headingZ + deltaX * headingX) >> 8
lateral      = abs(deltaX * headingZ - deltaZ * headingX) >> 8
vertical     = abs(candidateY - currentY)
```

All intermediate operations use the original R3000A 32-bit wrapping and signed
comparison/division behavior. A candidate is added when all three values are
within the active profile's limits. Near, Mid, and Far union those candidates
with the stock list. Stock leaves the original owner result untouched. Low
deliberately uses geometry only and omits stock-authored exceptional entries,
matching replacement-engine mode 2.

| value | longitudinal | lateral | max-planar | mode |
| --- | ---: | ---: | ---: | ---: |
| Low | 16000 | 14000 | 12000 | 2 (geometry only) |
| Stock | 40000 | 34000 | 32000 | 1 (stock owner only) |
| Near | 40000 | 34000 | 32000 | 0 (union) |
| Mid | 56000 | 48000 | 44000 | 0 (union) |
| Far | 65535 | 65535 | 65535 | 0 (union) |

All geometric profiles use the fixed vertical limit 32766 (`0x7FFE`).

The authenticated FAR descriptor is the final ten-halfword profile at patched
address `0x800D23E0`:

```text
FFFF 0000  FFFF 0000  FFFF 0000  0000 0000  0001 0000
```

The three effective visibility limits are therefore:

- longitudinal: 65535;
- lateral: 65535;
- vertical: 32766 (`0x7FFE`, initialized by the replacement owner and not
  changed by the menu profile).

The remaining profile field requests a replacement-engine rebuild. It is
unnecessary when the stock owner remains in place and the host-native callback
computes the selected result directly.

## Direct implementation

The direct plugin runs at stock return instruction `0x801422F4`, after the
stock owner has selected heading-specific lists, advanced the scene mask, and
combined primary/secondary view bits. It reads the two state pointers already
published by stock at `0x801762D4/0x801762D8`, evaluates the selected profile,
and either unions or replaces the current stock mask according to its mode.
Stock returns zero from the callback so the original `jr ra` and delay slot
execute unchanged. The other modes model the skipped `jr ra` delay slot
(`addiu sp,sp,0x28`) and redirect to `ra`.

This design has these boundaries:

- no executable or disc patch bytes;
- no guest code island;
- no guest or high-RAM allocation;
- no writes to executable RAM;
- no hash bypass; and
- no Track 4x4 reads or writes.

## Focused proof

The synthetic C test exercises all five choices, inclusive profile boundaries,
primary/secondary union, Low removal of stock-only visibility, Stock bypass,
activation gating, and exact JR/delay semantics.

Independent comparison against three captured Hueponik FAR race states found:

| state | recovered geometric set | Hueponik decoded list | stock-only union |
| --- | ---: | ---: | ---: |
| slot 03 | 110 | 110 | 0 |
| slot 05 | 156 | 163 | 7 |
| slot 06 | 152 | 161 | 9 |

There were zero recovered candidates missing from any decoded list. The only
extra decoded entries were the expected stock-list members that Hueponik unions
before its FAR candidates. This is the same union performed by the direct
post-hook.
