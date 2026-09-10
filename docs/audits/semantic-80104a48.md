# Semantic audit: `0x80104A48`

## Result

`0x80104A48` is `ComputeRaceFrameRandom15OffsetA00`, the `+0xA00` deterministic
variant of the adjacent race/frame random sampler.  It derives a 15-bit value
from shared race state without advancing a PRNG.

## Boundary and ABI

The routine is frameless and unsplit.  It begins with two global-base loads at
`0x80104A48`, returns with `jr ra` at `0x80104A6C`, and masks the result in the
delay slot at `0x80104A70`.  The next independent owner begins at `0x80104A74`,
proving exact extent `[0x80104A48,0x80104A74)`, `0x2C` bytes.

No incoming argument register is read before being overwritten, so the ABI is:

```c
uint32_t ComputeRaceFrameRandom15OffsetA00(void);
```

## Exact formula and state

The function reads the confirmed shared words `gSelectedRacerSeed` at
`0x80173A8C` and `gRaceFrameCounter` at `0x80173AEC`, then computes:

```c
uint32_t sum = gSelectedRacerSeed + 0xA00u + gRaceFrameCounter;
uint32_t mixed = (uint32_t)((int32_t)sum >> 1) ^ (sum << 3);
return mixed & 0x7FFFu;
```

Both additions use wrapping `addu/addiu`.  The right shift is arithmetic and
propagates sum bit 31; the left shift discards high overflow.  XOR is masked to
`0..32767`.  The leaf performs no store, call, stack access, or hardware access.

This differs from stateful `NextRandom15`: it does not advance either input.
It is also independent from `ComputeRaceFrameRandom15` at `0x80104A1C` and the
`+0xF00` variant at `0x80104A74`; all three use the same mixer but distinct
pre-mix offsets.

## Callers and overlay reconciliation

There are exactly nine unique executable guest calls:

```text
800E36B8 801391CC 80139238 8014C7DC 8014CA58
8014E004 801559AC 8015945C 8015AC18
```

Grouped by owner, these are one in `0x800E30FC`, two in `0x80139148`, two in
`0x8014C704`, and one each in `0x8014DF60`, `0x80155550`, `0x801590C4`, and
`0x8015A45C`.

Generated output contains ten instances because guest site `0x8015945C`
appears in two mutually exclusive overlay variants.  The raw image contains
exactly the nine unique aligned JALs, with no opcode-shaped data false positive.
No external J or JAL targets an interior instruction.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS and
  the nine unique caller PCs;
- `generated/SCES_028.45_full.ranges` for the exact unsplit boundary;
- `docs/audits/semantic-80104a1c.md` for independently proven shared-state
  producers and the base sampler;
- generated overlay instances for the ten-instance/nine-PC reconciliation.
