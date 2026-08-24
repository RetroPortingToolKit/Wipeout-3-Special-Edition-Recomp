# Semantic audit: `0x8015F32C` / `ArmGpuTimeoutWatchdog`

## Result

`0x8015F32C` is the arming/reset half of the PsyQ GPU timeout watchdog, now
named `ArmGpuTimeoutWatchdog`:

```c
void ArmGpuTimeoutWatchdog(void);
```

It samples the software vertical-blank counter, sets the GPU deadline 240
vblanks in the future, and clears the watchdog's secondary call/spin counter.

## Exact owner and ABI

Generated metadata gives the entry an exact `0x34`-byte range. It opens a
`0x18`-byte frame, calls `VSync`, restores `ra`, and returns at
`0x8015F358..0x8015F35C`. The independently audited watchdog checker begins at
`0x8015F360`, so the exact owner is `[0x8015F32C,0x8015F360)`.

No incoming argument register is read. After `VSync(-1)`, `v0` is incremented
by 240 and remains live through return, but all five callers ignore it and the
routine's purpose is wholly expressed by its global stores. The evidence-backed
ABI is therefore argument-free and void; the deadline in `v0` is incidental
register residue rather than a consumed return contract.

## Exact behavior and checker relationship

The complete semantic operation is:

```c
void ArmGpuTimeoutWatchdog(void) {
    uint32_t now = VSync(-1);
    *(uint32_t *)0x80179084 = now + 240;
    *(uint32_t *)0x80179088 = 0;
}
```

Addition wraps modulo 32 bits. There are no GPU MMIO accesses, interrupt-mask
changes, loops, or recovery actions in this helper.

The adjacent confirmed `CheckAndRecoverGpuTimeout` at `0x8015F360` reads these
same words. It compares a fresh `VSync(-1)` sample against `0x80179084` and,
while the deadline remains healthy, advances the counter at `0x80179088` up to
its secondary guard. If either guard expires, that routine performs diagnostics
and GPU/DMA recovery. This exact producer-consumer relationship establishes
the `ArmGpuTimeoutWatchdog` identity and the meaning of both stores.

## Exhaustive callers and interior targets

An aligned scan of the retained executable finds five unique JALs:

```text
8015E320  8015E5E4  8015E81C  8015EBB4  8015F200
```

All target the true entry, and no J/JAL targets an interior instruction.
Generated output also emits exactly five calls, so there is no overlay
duplication.

Every caller is in the neighboring GPU library. They arm the watchdog before
starting or waiting on GPU/DMA work; the longer paths subsequently call
`CheckAndRecoverGpuTimeout` inside their wait loops. No site reads `v0` after
the arming call.

## Sources checked

- `disc/SCES_028.45`, accounting for its `0x800`-byte PS-X EXE header, for
  exact instructions, unique callers, and target scans;
- `generated/SCES_028.45_full.ranges` for the exact owner boundary;
- generated caller windows for GPU-library context and return consumers;
- `docs/audits/semantic-8015f360.md` for the independently proven watchdog
  checker and recovery-side consumption of the two state words.
