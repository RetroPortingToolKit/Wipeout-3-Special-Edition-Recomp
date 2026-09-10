# Semantic audit: `0x801224B4`

## Result

`0x801224B4` is `SetSoundSlotVolume`, a guarded game-side adapter which writes
the volume byte of one live sound-slot record and dispatches the changed slot
to the active audio backend.

## Boundary and ABI

The function allocates a 24-byte frame at `0x801224B4`, saves `ra` in the
first branch delay slot at `0x801224C8`, and has six basic blocks.  All reject
paths and the update path join at `0x80122508`.  It restores `ra`, returns at
`0x80122510`, and restores `sp` in the delay slot at `0x80122514`.  The next
independent owner begins at `0x80122518`, proving the exact half-open range
`[0x801224B4,0x80122518)`, 100 bytes, matching `R 801224B4 64`.

The recovered ABI is:

```c
void SetSoundSlotVolume(int32_t sound_slot, uint32_t volume);
```

`a0` selects a slot, `a1` is truncated to its low byte at `0x801224C0`, and
`a2`/`a3` are unused.  No path establishes a semantic return value.

## Guards and state mutation

The routine returns without sound-slot mutation or a callee when any guard
fails:

1. global byte `0x80175E68` is zero (audio disabled);
2. `sound_slot == -1`;
3. unsigned `sound_slot >= 24` (therefore other negative values are rejected);
4. bit `0x2` is clear in the selected record's flags word at `+0x08`.

Records are 32 bytes rooted at `0x801D674C`.  On success the branch delay slot
at `0x80122504` stores `u8(volume)` to record byte `+0x1A`, the independently
identified sound-instance volume field.  The adapter then calls
`0x8012257C(sound_slot)` with the original slot index still in `a0`.

The downstream routine is the audio backend dispatcher already established by
the `0x80121534` audit: backend mode byte `0x801D6E55` selects `0x80121C6C` for
mode 1 or `0x80121D4C` for mode 2.  Thus this adapter changes authoritative
queued sound state and requests backend propagation; it does not directly
write SPU registers.  The byte store occurs before the call, so an unsupported
backend mode can suppress propagation but does not undo the slot update.

## Direct callers

An exhaustive aligned JAL scan of the loaded EXE finds exactly 20 sites,
grouped by generated owner:

- `0x8013665C`: `0x801366F4`
- `UpdateLoopingSoundChannel` (`0x8013670C`): `0x80136990`
- `0x8013782C`: `0x8013785C`, `0x80137868`, `0x80137874`, `0x80137880`,
  `0x8013788C`, `0x80137898`, `0x801378A4`, `0x801378B0`
- `0x8013791C`: `0x8013794C`, `0x80137958`, `0x80137964`, `0x80137970`,
  `0x8013797C`, `0x80137988`, `0x80137994`, `0x801379A0`
- `0x80137F84`: `0x80138000`, `0x80138054`

The two eight-call clusters are consistent with batch updates over fixed sound
slots.  No direct call targets an interior block of the adapter.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS
  instructions and the exhaustive aligned JAL scan;
- `generated/SCES_028.45_full.ranges` and the generated six-block owner;
- `docs/audits/semantic-80121534.md` and
  `docs/audits/semantic-8013670c.md` for independently established slot layout,
  volume field, and backend-dispatch semantics.
