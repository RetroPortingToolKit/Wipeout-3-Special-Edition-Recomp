# Semantic audit: `0x8015FD4C` / `SetSemiTrans`

## Result

`0x8015FD4C` is the canonical Psy-Q `SetSemiTrans` packet helper:

```c
void SetSemiTrans(void *primitive, int enable);
```

It reads the GPU primitive code byte at `primitive + 7`. A nonzero `enable`
ORs that byte with `0x02`; zero ANDs it with `0xFD`. The byte is written back
in the return delay slot, preserving every other primitive-code bit. It has no
semantic scalar return.

## Boundary and effects

The exact range is `[0x8015FD4C, 0x8015FD74)`, `0x28` bytes. Four basic blocks
implement the enable branch and common return. The next aligned owner begins
at `0x8015FD7C`; the intervening padding is not part of this function.

The only shared effect is one byte read-modify-write through `a0 + 7`. There
are no calls, stack frame, global accesses, GPU/GTE instructions, validation,
or synchronization. A null or invalid packet pointer therefore faults under
the original contract.

## Complete incoming proof

An aligned executable scan finds exactly 57 direct JAL sites, all to the entry
and none to the internal blocks at `0x8015FD54`, `0x8015FD60`, or
`0x8015FD6C`:

```text
800F5064 800F76EC 800F76F8 800F7704 800F7710 80104C50 80105204 801052D8
801094F0 801095C8 80109680 8010975C 80109830 801098E0 80109A44 8010A25C
8010A34C 8010A418 8010B12C 8010B170 8010BD5C 8010C044 8010C1A0 8010C210
8010C26C 8010C2D8 8010C330 8010C398 8010C3F4 8010C450 8010C4B4 8010C828
8010CAA4 8010CBA4 8010CCA0 8010CE04 8010D330 8010D388 8010EF4C 8010EF5C
8010EFB8 8010F010 8010F070 8010F0C0 8010F114 8010F158 80110768 8011CC00
8011CCBC 8011CD78 80140AE4 80140BEC 80141880 80149810 8014A668 8014DC64
80156E20
```

The distribution across font, menu, race, and effect renderers is consistent
with a shared SDK primitive helper. The recovered localized glyph emitter at
`0x8010EAC4` independently proved the same bit-1 behavior.

## Confidence

`confirmed`: canonical identity, ABI, exact range, bit operation, sole memory
effect, all callers, and absence of interior entries are instruction-proven.
