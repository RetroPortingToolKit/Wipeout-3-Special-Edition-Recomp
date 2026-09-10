# Semantic audit: `0x8015F360`

## Result

`0x8015F360` is an argument-free Psy-Q GPU watchdog and recovery routine,
conservatively named `CheckAndRecoverGpuTimeout`. It returns zero while the GPU
wait remains within its guards; on an expired frame/count guard it reports
diagnostics, resets tracked GPU/DMA state under an interrupt mask, and returns
-1.

## Boundary and ABI

The generated manifest gives exact owner range `0x8015F360..0x8015F4A3`
(0x144 bytes). The next independent owner starts at `0x8015F4A4`. Entry creates
a 32-byte frame and saves `$ra`; the common epilogue at `0x8015F494..A3`
restores it and returns. There is no padding or semantic spill into the next
owner.

An evidence-backed prototype is:

```c
int CheckAndRecoverGpuTimeout(void);
```

Incoming argument registers are never read. The normal path returns 0 and the
recovery path returns -1.

## Watchdog decision

The routine first calls confirmed `VSync(-1)` at `0x8015F904`. It compares the
returned vertical-blank count against tracked word `0x80179084`. When the
tracked value is not behind the returned count, it increments watchdog word
`0x80179088`; values through `0x000F0000` return zero without touching hardware.
An expired frame comparison or a count beyond that limit enters recovery.

The recovery diagnostic calls confirmed `Printf` with retained format string
`0x8017F18C` and snapshots derived from the GPU tracking pointers at
`0x80179050`, `0x80179054`, `0x8017905C`, `0x80179070`, and `0x80179074`.

## Recovery effects

Recovery calls confirmed `SetIntrMask(0)` and saves its returned prior mask.
It then:

- clears tracked word `0x80179074` and copies that cleared value to
  `0x80179070`;
- stores the prior interrupt mask in `0x80179080`;
- writes `0x00000401` through the pointer at `0x8017905C`;
- ORs `0x00000800` into the word addressed by pointer `0x8017906C`;
- writes GP1 reset/control words `0x02000000` and then `0x01000000` through
  the active GPU control-register pointer at `0x80179050`;
- calls `SetIntrMask` again with the saved mask at `0x80179080`.

It then returns -1. The function performs no caller-selected operation and does
not directly write COP0; interrupt masking is delegated to `SetIntrMask` and
affects I_MASK rather than COP0 Status.

## Calls and targets

Direct callees are `VSync`, `Printf`, and `SetIntrMask` (twice on recovery).
Generated labels `0x8015F370`, `388`, `3B4`, `404`, `40C`, `488`, `490`, and
`494` are internal continuation blocks. No external call or branch targets an
interior address.

There are exactly seven generated and seven unique executable `jal` callers,
all in the neighboring GPU library:

`0x8015E34C`, `0x8015E6CC`, `0x8015E900`, `0x8015E9AC`, `0x8015EBC4`,
`0x8015F218`, and `0x8015F24C`.

There are no overlay duplicates or data-derived false callers; every call
targets the true entry `0x8015F360`.
