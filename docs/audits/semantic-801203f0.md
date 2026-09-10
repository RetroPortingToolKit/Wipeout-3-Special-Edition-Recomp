# Semantic audit: `0x801203F0` / `ApplyAnalogControllerCalibration`

## Result

`0x801203F0` is the in-place analog controller calibration stage now named
`ApplyAnalogControllerCalibration`:

```c
uint8_t *ApplyAnalogControllerCalibration(unsigned logical_pad);
```

It resolves one of eight logical pads (two physical ports times four multitap
slots), selects its live response packet, recognizes analog packet headers
`0x7300` and `0x2300`, applies the pad's calibration and history state to the
payload, and returns that packet. Invalid, unavailable, unsupported, or
nonexistent selections return null.

## Exact owner and false generated splits

The true callable is `[0x801203F0,0x801207A4)`, length `0x3B4`. It is frameless,
contains no calls, and has returns at `0x80120444`, `0x80120588`, and the shared
epilogue `0x80120798..0x801207A0`. The next independent function creates a
stack frame at `0x801207A4`.

The generated entries at `0x80120444`, `0x8012044C`, `0x80120454`,
`0x80120590`, and `0x80120798` are internal blocks, not callables. Branches
from the entry prelude select them, and an exhaustive aligned JAL scan finds no
call to any interior address. They must remain owned by `0x801203F0`.

## Selection and ABI

`a0` is an unsigned logical-pad number. Values 8 and above return null. The
routine computes physical port `a0 >> 2`, slot `a0 & 3`, and indexes the two
40-byte port records at `0x801D5488`. A nonzero record byte `+0` rejects the
port. When record byte `+1` is `0x80` (the multitap response form), the packet
is selected at `record + 2 + 8*slot`; otherwise only slot zero is legal and the
record itself is the packet.

The return is the selected packet pointer for either supported analog format,
including after mutation, and zero on all rejection paths. All seven callers
consume it as an optional packet pointer. No callee-saved register, stack,
hardware register, callback, or interrupt state is involved.

## Calibration behavior

Per-logical-pad calibration occupies 16 bytes at `0x80175D64 + 16*a0`.
Current and previous filtered values occupy eight-byte records at
`0x80175DE8 + 8*a0` and `0x80175E28 + 8*a0`.

For header `0x7300`, four payload bytes beginning at packet `+4` are centered
against the stored origin, suppressed inside a symmetric deadzone, scaled in
fixed point, and clamped to signed `[-128,127]`. The centered results are
written back into the packet's four axis bytes while current/previous
halfword histories are advanced.

For header `0x2300`, the first analog field is likewise centered, deadzoned,
scaled, and clamped to `[-128,127]`. The remaining three fields use the second
calibration pair as one-sided pressure/travel values: values below threshold
become zero, positive travel is scaled, and the result is saturated at 255.
Those bytes and their histories are updated in place. Other packet headers are
not modified and return null.

The name deliberately describes the common proven operation without assigning
an original source identifier. The response IDs, four-axis versus
steering-plus-pressure layouts, and multitap indexing support analog-pad and
NegCon-class interpretation; the arithmetic itself is the decisive naming
evidence.

## Complete caller and target census

The retained executable contains exactly seven direct calls, all to the true
entry:

```text
801167C0
80122B34 80122B88 80122D4C 80122DA0
8012350C 80123560
```

They pass logical pad zero or four (the first slot of physical ports one and
two), then pass the optional result to `CopyAndNormalizeControllerPacket`.
That downstream routine copies eight bytes, preserves the raw header, and
supplies neutral controls for a null or unsupported response. The startup
caller additionally tests `0x2300`, `0x4100`, and `0x7300` after this copy.
There are no direct jumps or calls from outside the owner to an interior block.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact
  instructions, globals, all aligned JAL targets, and caller windows;
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_full_32.c` for the misleading generated splits and
  complete internal control flow;
- `docs/audits/semantic-80120988.md` for the immediately downstream packet
  copy/normalization contract.
