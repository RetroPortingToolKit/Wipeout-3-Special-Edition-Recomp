# Semantic audit: `0x8014B3D4` / `AbsoluteValue32`

## Result

`0x8014B3D4` implements signed 32-bit integer absolute value:

```c
int32_t AbsoluteValue32(int32_t value) {
    return value < 0 ? 0 - value : value;
}
```

The subtraction is ordinary MIPS `subu`, so it does not trap on overflow.
Consequently `INT32_MIN` returns the same `0x80000000` bit pattern rather than
saturating or widening.

## Boundary and effects

The exact owner is `[0x8014B3D4, 0x8014B3EC)`, six instructions or `0x18`
bytes. Three basic blocks cover entry, the negative-only negate, and the common
return. The next independent owner starts at `0x8014B3EC`.

It has no frame, callees, loads, stores, global access, coprocessor operations,
or hardware effects. The only input is `a0`; the only contract result is `v0`.

## Complete incoming proof

An aligned executable scan finds exactly 21 direct JALs, all to the entry and
none to the internal blocks at `0x8014B3E0` or `0x8014B3E4`:

```text
800E37E8 800E3854 800E4274 800E4B50 800E4B70
800EFDB0 800EFDBC 800F4A48 800F4A78
800FBAA4 800FBAC0 800FBADC 800FBB60
8010BA88 8011CFA0 8011CFB8 8012C25C 8012C270
80147F34 80154000 801591D0
```

## Confidence

`confirmed`: exact range, signed condition, wraparound negate, scalar return,
effect-free behavior, every caller, and lack of interior entries are direct
instruction-level facts.
