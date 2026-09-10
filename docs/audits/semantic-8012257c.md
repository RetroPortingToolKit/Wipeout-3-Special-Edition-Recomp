# Semantic audit: `0x8012257C`

## Result

`0x8012257C` is the confirmed audio bridge
`DispatchSoundSlotUpdateToBackend`. It reads the active backend mode and
forwards one sound-slot index to exactly one of two queued-voice update
routines. It does not validate or modify the slot itself.

Its recovered ABI is:

```c
void DispatchSoundSlotUpdateToBackend(int32_t sound_slot);
```

## Exact boundary

The preceding owner restores its own 24-byte frame and returns at
`0x80122574`, with its stack restoration at `0x80122578`.
`DispatchSoundSlotUpdateToBackend` opens a new 24-byte frame at `0x8012257C`,
saves only `ra`, and joins all paths at a common epilogue. It returns at
`0x801225C8` and restores the frame in the delay slot at `0x801225CC`. The
next independent byte-swap leaf begins at `0x801225D0`.

The exact half-open range is therefore `[0x8012257C,0x801225D0)`, or `0x54`
bytes, exactly matching the generated manifest. Generated entries at
`0x80122594`, `0x801225A0`, `0x801225A8`, `0x801225B0`, `0x801225B8`, and
`0x801225C0` are internal mode branches/post-call continuations retaining the
owner's frame. An exhaustive aligned scan finds no call to an interior
address.

## Complete caller set

There are exactly five direct callers:

```text
801217BC 80121A1C 80122438 8012249C 80122500
```

The first two are the final submission calls in the two sound-slot/voice
allocation owners ending at `0x801217F4` and `0x80121A58`. They invoke the
dispatcher after building authoritative slot state and return the allocated
slot index independently of this dispatcher's result.

The other three are guarded live-slot setters:

| Caller | Slot byte written before dispatch | Meaning |
|---:|---:|---|
| `0x801223EC` via call `0x80122438` | `+0x1B` | pan |
| `0x80122450` via call `0x8012249C` | `+0x1C` | pitch |
| `SetSoundSlotVolume` via call `0x80122500` | `+0x1A` | volume |

Each setter validates audio enablement, slot range, and active flags before
performing its byte store in the JAL delay slot. Therefore by entry here, the
new authoritative slot value is already visible. This dispatcher neither
duplicates those guards nor rolls back the store when no backend is selected.

## Mode dispatch

The function reads one unsigned byte from `gAudioBackendMode` at
`0x801D6E55` with `lbu`. Its entire dispatch is:

```c
switch (gAudioBackendMode) {
case 1:
    func_80121C6C(sound_slot);
    break;
case 2:
    func_80121D4C(sound_slot);
    break;
default:
    break;
}
```

`a0` is never changed before either JAL, so the signed 32-bit bit pattern from
the caller is forwarded exactly. Modes `0`, `3..255`, and every other byte
outside `1/2` take the no-call path. There is no fallback from one backend to
the other and no validation against the 24-slot bound.

## Backend and state semantics

Both callees derive a 32-byte sound-slot record as:

```text
slot = 0x801D674C + 32*sound_slot
```

and propagate its volume/pan/pitch and voice ownership into 36-byte queued
voice records rooted at `0x801D63EC`:

- mode 1 (`0x80121C6C`) applies master-scaled volume to the primary and, when
  present/owned, secondary voice records;
- mode 2 (`0x80121D4C`) additionally derives left/right levels from slot pan,
  pitch, and class/control flags before updating owned voice records.

Those mutations belong to the selected backend callee, not this dispatcher.
The dispatcher itself writes only its stack frame. It does not touch the
sound-slot table, voice table, SPU registers, audio-enable globals, or the
backend-mode byte.

This distinction explains existing call chains precisely:

```text
slot allocator or guarded setter
    -> authoritative 0x801D674C slot mutation
    -> DispatchSoundSlotUpdateToBackend(slot)
    -> selected 0x801D63EC queued-voice mutation
```

Neither backend directly writes SPU hardware in the inspected routines; they
mark/update queued voice state for the lower audio service path.

## Return semantics

No stable scalar return exists. For modes 1 and 2, `v0` is whatever the
selected backend happens to leave behind. On unsupported modes, `v0` retains
an internal comparison constant. All five callers ignore it. The ABI is
therefore correctly modeled as `void`, not as a status or slot-returning
function.

## Evidence

- `disc/SCES_028.45`: exact instructions and complete entry/interior JAL scan;
- `generated/SCES_028.45_full.ranges`: exact `0x54` range;
- `generated/SCES_028.45_full_33.c`: complete mode branches and both backend
  routines;
- `docs/audits/semantic-80121534.md`: slot allocation/voice-state producer;
- `docs/audits/semantic-801224b4.md`: guarded volume setter and dispatch order;
- `docs/audits/semantic-8013670c.md`: independently recovered pan/pitch/volume
  slot-field meanings.
