# Semantic audit: `0x8015F7A4` / `SetPolyFT4`

## Result

`0x8015F7A4` is the canonical Psy-Q `SetPolyFT4` initializer:

```c
void SetPolyFT4(POLY_FT4 *packet);
```

It writes `9` to packet byte `+3`, the GPU DMA payload length, and `0x2C` to
packet byte `+7`, the opaque flat-textured four-point polygon opcode. The
opcode store executes in the return delay slot.

## Boundary and effects

The exact owner is `[0x8015F7A4, 0x8015F7B8)`, five instructions or `0x14`
bytes, in one frameless basic block. The following bytes are padding before the
next independent owner at `0x8015F7C4`.

Only those two bytes are mutated. The low 24 bits of the packet tag and all
RGB, UV, CLUT, TPAGE, and four XY fields remain caller-owned. There are no
loads, callees, globals, validation, synchronization, or direct GPU writes. No
caller consumes the incidental `v0 = 0x2C` value, so the SDK contract is void.

## Complete incoming proof

An aligned executable scan finds exactly twenty JALs, all to the entry and no
interior targets:

```text
800EE4D8 800F4FC8 80104C30 80105108 80105140 80105214
8010C81C 8010CA98 8010CB98 8010CC94 8010CDF8
80115ED4 8011CB2C 8011CC18 8011CCD4 8012DE04
80138F68 8013904C 801454AC 8014DC58
```

Caller cursor advances of 40 bytes independently confirm the `POLY_FT4`
layout, while later `SetSemiTrans` calls selectively toggle opcode bit 1.

## Confidence

`confirmed`: canonical identity, ABI, exact range, two byte writes, preserved
packet state, all callers, and absence of interior entries are exact facts.
