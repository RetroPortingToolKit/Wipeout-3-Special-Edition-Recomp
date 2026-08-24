# Semantic audit: `0x80104AA0`

## Result

The argument-free leaf at `0x80104AA0` is named `NextRandom15`. It advances a
private 32-bit XOR/shift PRNG state and returns the low 15 bits of the newly
stored state:

```c
uint32_t NextRandom15(void)
{
    uint32_t old = gRandom15State;
    uint32_t next = (uint32_t)((int32_t)old >> 1) ^ (old << 3);
    gRandom15State = next;
    return next & 0x7FFF;
}
```

This is the title's dominant general-purpose random-number generator: 254
direct calls use its bounded result for effect, animation, sound, and race
variation. It is not the PsyQ/ANSI linear-congruential `rand` algorithm, so the
symbol records its observable result width rather than assigning it a library
identity.

## Exact algorithm, state, and boundary

- `0x80104AA0..A4` loads the word at `0x80173A94`.
- `0x80104AAC` performs a signed arithmetic right shift by one. This detail is
  part of the state recurrence; replacing it with a logical shift changes the
  upper half of later states.
- `0x80104AB0..B4` computes the original word shifted left by three, modulo 32
  bits, then XORs it with the arithmetic-right-shift result.
- `0x80104AB8` stores that full unmasked word back to `0x80173A94` before the
  return.
- `0x80104ABC` is `jr ra`; its delay slot at `0x80104AC0` masks only `v0` with
  `0x7FFF`. Thus every public result is in `0..32767`, while the recurrence
  retains all 32 bits.
- The PS-X EXE image initializes `0x80173A94` to `0x12345678`. The adjacent
  words at `0x80173A90` and `0x80173A98` have the same initial value but are
  separate streams; the near-identical leaf at `0x8015B5D8`, for example,
  advances `0x80173A98`, not this state.
- The generated range is exactly `0x24` bytes and has one basic block. The next
  independent callable begins with a 48-byte stack-frame prologue at
  `0x80104AC4`. No generated continuation or fallthrough belongs to the leaf.

The only retained main-EXE instructions that directly load or store
`0x80173A94` are the leaf's `lw` and `sw`, establishing it as private state.

## Complete caller census

Scanning every aligned instruction word in the retained PS-X EXE payload for
JAL encoding `0x0C0412A8` finds exactly 254 direct callers:

```text
800EF404 800EF438 800EF448 800EF45C 800EF46C 800EF47C 800EF48C
800EF4EC 800EF504 800EF51C 800EF534 800EF544 800EF554 800EF56C
800EF584 800EF59C 800EF5AC 800EF5CC 800EF5D8 800EF6BC 800EF6CC
800EF6DC 800EF6EC 800EF70C 800EF728 800EF738 800EF760 800EF768
800F6B20 800F6B68 800F6BAC 800F6E40 800F6E58 800F6F5C 800F8BF8
800F8C10 800F8C28 800F8C40 800F8C58 800F8C70 800F9158 800F9178
800F9198 800F91B8 800F91F4 800F9214 800F9234 800F9254 800F949C
800F94D4 800F94F8 800F9538 800F9600 800F9620 800F9640 800F9660
800F9688 800F96E8 800F9A3C 800FA420 800FABAC 800FABB8 800FABC4
800FAC34 800FAC44 800FAC54 800FB530 800FB548 800FB558 800FB568
800FB80C 800FB824 800FB834 800FB844 800FB878 800FB994 800FBA3C
800FBC50 800FBC84 800FC0BC 800FC0C8 800FC0D4 800FC144 800FC154
800FC650 800FC778 800FCA58 800FCB90 800FCDD8 800FCF14 800FD0E8
800FD1CC 8011C79C 8011C7DC 8011C818 8011C840 8011C89C 8011DB28
8011DBC8 8011DC08 8011DC74 8011DCAC 8011DCE4 8011DEA8 8011E348
8011E384 8011E3EC 8011E41C 8011E6C8 8011E7A4 8011E800 8011E84C
8011E870 8011E88C 8011E894 8011E89C 8011E8C0 8011E8C8 8011E8D0
8011E8F4 8011E8FC 8011E904 8011EB4C 8011EB70 8011EB94 8011EBB8
8011EF9C 8011EFAC 8011EFB8 8011EFC8 8011F008 8011F030 8011F2C4
8011F2E4 8011F2F4 8011F300 8011F310 8011F34C 8011F37C 8011F4A0
8011F4B0 8011F4BC 8011F50C 8011F51C 8011F52C 80138080 80138410
8013844C 80139320 80139338 80139344 80139350 801393F0 801393FC
80139408 80139500 80139510 80139520 80139568 801395A4 801395B0
801395BC 801395FC 80139608 80139614 8013A764 8013C400 8013C418
8013C430 8013C448 8013C460 8013C478 8013C8CC 8013C918 8013C9F0
8013C9FC 8013CA08 8013CA14 8013CA20 8013CC6C 8013CC94 8013CCC0
8013CCD0 8013CCE0 8013CD48 8013CD70 8013CD90 8013CDA8 8013CDB8
8013CDC8 8013CEF0 8013CF08 8013CF24 8013D014 8013D02C 8013D048
8013D948 8013D960 8013D9CC 8013D9D4 8013D9F4 8013D9FC 8013DC54
8013E758 8013E774 8013E840 8013E850 8013E864 8013E874 8013EA30
8013EEC8 8013F20C 8013F25C 8013F288 8013F290 8013F2BC 8013F2E8
8013F904 80140660 80140EA8 80140EB8 80140EC8 80140ED8 80140EE4
80140F50 80141030 80141048 80141060 80146630 80147364 801476A4
801476B8 801476D8 801476EC 8014CD5C 8014CDE4 8014CE10 8014FAE0
8014FAF8 8014FB08 8014FFC8 8014FFD4 8014FFDC 8014FFE4 80150020
801504D8 801504E4 801504EC 801504F4 8015612C 8015B814 8015B824
8015B834 8015B848
```

Generated consumers use the result as randomness rather than an opaque status
code. For example, the routine at `0x8014FAE0` immediately masks one result
with bit `0x0004`, while many dense effect initializers call it repeatedly to
derive independent record fields. The broad subsystem spread and repeated
mask/scale use establish its original PRNG role.

There are no aligned JALs to `0x80104AA4`, `0x80104AB8`, or `0x80104ABC`, so
none of the leaf's interior instructions is an alternate callable entry.

## Sources checked

- `disc/SCES_028.45`: authoritative initial state and complete JAL scan.
- `generated/SCES_028.45_full.ranges`: exact `0x24` generated range.
- `generated/SCES_028.45_full_106.c`: complete leaf and adjacent next entry.
- `generated/SCES_028.45_full_100.c` through `_132.c`: callers and downstream
  uses of the 15-bit result.
- `generated/SCES_028.45_full_132.c`: independent same-algorithm stream at
  `0x8015B5D8` using state `0x80173A98`.
