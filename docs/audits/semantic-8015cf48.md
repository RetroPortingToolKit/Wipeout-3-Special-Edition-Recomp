# Semantic audit: `0x8015CF48` / `Printf`

## Result

`0x8015CF48` is the PlayStation BIOS A0/3F `printf` trampoline, cataloged as
`Printf` to match the project's exported-symbol style:

```c
int Printf(const char *format, ...);
```

The thunk loads `t1 = 0x3F`, jumps to BIOS vector `0xA0` through `t2`, and
places the selector load in the jump delay slot. It does not touch the argument
registers, variadic stack arguments, return address, or BIOS result in `v0`.

## Boundary and control flow

The exact callable range is `[0x8015CF48, 0x8015CF58)`, four instructions or
`0x10` bytes. The instruction at `0x8015CF54` is unreachable padding after the
indirect BIOS jump; it is not a second callable entry. The next independent
thunk starts at `0x8015CF58`.

There is no ordinary JAL, stack frame, RAM access, or direct hardware access.
All formatted parsing, output effects, and the integer return originate in BIOS
function A0/3F.

## Complete caller proof

An aligned executable scan finds exactly 65 direct JALs, every one targeting
`0x8015CF48` and none targeting the padding:

```text
800E58F8 800FFE58 801001BC 801001D8 801001F4 80100210 8010022C 80100250
80100274 80100298 801002BC 801002E4 80100308 8010032C 80111D80 801138F4
8011693C 80116950 80116B24 80116B38 80116EA8 80116F50 80116F60 80116FC0
8015CFD8 8015F3FC 80160398 80160494 801604D0 80160D9C 801627C8 801628FC
801636C0 80163984 80163C0C 80163F68 80163FD0 80164000 80164068 801640DC
80164194 801641D4 8016422C 80164258 80164354 801643C0 80164524 80164550
8016467C 80164700 801677B4 80167800 80167B18 80167C54 80167ED4 801680DC
80168130 80168330 801686FC 801689B4 80168EE4 80168F0C 8016AE08 8016B0C8
8016B15C
```

The callers span startup diagnostics, heap diagnostics, CD/BIOS support, and
other retained-library code, which independently agrees with shared `printf`.

## Confidence

`confirmed`: BIOS vector, selector, variadic ABI preservation, exact range,
return propagation, all callers, and absence of interior entries are direct
instruction-level facts.
