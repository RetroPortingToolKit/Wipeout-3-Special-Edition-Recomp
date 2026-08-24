# Semantic audit: `0x801217F4`

## Result

`0x801217F4` is the confirmed persistent-sound creator
`CreatePersistentSoundSlot`. It allocates a sound-instance slot, records a
resource plus live volume/pan/pitch/scale state, allocates one or optionally
two ordinary queued voices, submits the slot to the selected audio backend,
and returns the slot index as a handle retained by looping/ambient callers.

Its recovered ABI is:

```c
int32_t CreatePersistentSoundSlot(
    int32_t resource_index,
    uint32_t volume,
    uint32_t pan,
    uint32_t pitch,
    int32_t pitch_scale_q8,
    uint32_t class_flags);
```

`volume`, `pan`, and `pitch` are truncated to their low bytes. Arguments five
and six are loaded from the caller's original `sp+0x10` and `sp+0x14` after
the function opens its own frame.

## Exact boundary and control ownership

The neighboring transient allocator restores its frame and returns at
`0x801217C4..0x801217F0`. This owner opens a distinct 56-byte frame at
`0x801217F4`, saves `ra`, `fp`, and `s0..s7`, and joins all exits at the
restore sequence beginning `0x80121A28`. It returns at `0x80121A50` and
restores `sp` at `0x80121A54`. The next owner begins independently at
`0x80121A58`.

The exact interval is `[0x801217F4,0x80121A58)`, or `0x264` bytes, matching
the generated range. Generated labels at `0x80121840`, `0x80121854`,
`0x8012185C`, `0x80121868`, `0x801218C8`, `0x801218D4`, `0x801218DC`,
`0x80121940`, `0x80121944`, `0x80121994`, `0x8012199C`, `0x801219A8`,
`0x80121A18`, `0x80121A1C`, `0x80121A24`, and `0x80121A28` are internal
branches/post-call continuations retaining the owner's frame. An exhaustive
aligned JAL scan finds no call to any interior address.

## Complete caller census and persistent role

There are exactly nine direct callers:

```text
80136504 8013652C 8013654C 80136570 80136590
801365B0 801365D0 801365F0 80136940
```

The first eight calls are in the ambient/looping sound initializer. Their
returned values are stored in a persistent handle array rooted at
`0x801E157C`; they create several resource-table sounds with initial volume
zero, pan/pitch 15, Q8 scales of `-1` or `0x100`, and class flags zero or 8.
The ninth call is in `UpdateLoopingSoundChannel`, which stores the returned
handle in its `(selector, channel)` handle slot and subsequently updates that
same slot through the recovered pan, pitch, pitch-scale, and volume setters.

This retained-handle use and the slot's state word `3` distinguish the owner
from `AllocateAndQueueSoundVoice` at `0x80121534`, whose slot state is `1` and
whose callers do not receive a meaningful handle.

## Admission and sound-slot allocation

The routine first reads audio-enable bytes `0x80175E68` and `0x80175E6D`. If
either is zero it skips all allocation, mutation, and backend calls. Notably,
those two early exits do not install the normal failure sentinel: `v0` still
contains the internal `lui 0x8017` value (`0x80170000`). Retained callers
invoke this creator only in audio-enabled paths, but the exact ABI edge case
is not `-1` when globally disabled.

With audio enabled it calls `0x801225F8`, which scans 24 records of stride
`0x20` rooted at `0x801D674C` and returns the first slot whose flags word has
bit zero clear. Pool exhaustion returns `-1`, which this owner propagates.

For allocated slot `s`, it writes:

| Slot offset | Value |
|---:|---|
| `+0x00` | 0 |
| `+0x04` | resource descriptor word `+0x10` |
| `+0x08` | persistent active state `3` |
| `+0x0C` | incoming `class_flags` |
| `+0x10` | incoming `pitch_scale_q8` |
| `+0x18` | primary voice index after allocation |
| `+0x19` | secondary voice index, or `-1` when no second voice requested |
| `+0x1A` | `u8(volume)` |
| `+0x1B` | `u8(pan)` |
| `+0x1C` | `u8(pitch)` |

The resource index selects a 20-byte descriptor at
`0x801D59EC + 20*resource_index`; descriptor word `+0x10` is copied into the
slot. There is no resource-index bounds check.

## Primary queued-voice state

The owner calls ordinary-voice allocator `0x80121F3C`. On success it records
the returned index in slot byte `+0x18`. Voice records are 36 bytes rooted at
`0x801D63EC`; for primary voice `v` it writes:

| Voice offset | Value/effect |
|---:|---|
| `+0x00` | preserve word and OR control bits `0x90` |
| `+0x04` | resource descriptor word `+0x00` |
| `+0x08` | 1 |
| `+0x0C` | 1 normally; changed to 2 when a second voice is requested |
| `+0x18` | signed `(pitch_scale_q8 * descriptor[+0x10]) / 256` |
| `+0x1C` | owning sound-slot index |

The division by 256 truncates toward zero by adding 255 to a negative product
before the shift. Unlike the neighboring general allocator, this persistent
creator has no fixed/reserved-voice argument and no `class_flags & 0x10`
control-bit special case; its primary voice always comes from the ordinary
allocator and receives only the preserved control word OR `0x90`.

If primary voice allocation returns `-1`, the function returns `-1` and does
not dispatch. However, it does not undo the slot writes already performed:
the selected slot remains marked state `3` with its resource and live control
bytes. This is an exact leak/orphan edge case under total ordinary-voice
exhaustion, not a transactional rollback.

## Optional second voice

When `class_flags & 8` is clear, the routine writes `-1` to slot byte `+0x19`
and proceeds to dispatch. When the bit is set, it changes the primary voice's
`+0x0C` word from 1 to 2 and calls `0x80121F3C` again.

On secondary success it records the second index at slot `+0x19` and
initializes that voice with the same resource word, scaled pitch, owning slot,
active words, and `0x90` control bits as the primary. On secondary failure it
still dispatches and returns the valid sound-slot handle, but does not write a
failure sentinel to slot `+0x19`; that byte retains its prior contents from
the reused slot record. This is another exact non-transactional edge case.

## Backend effects and return contract

After every primary-voice success, the routine calls
`DispatchSoundSlotUpdateToBackend(sound_slot)`. The independently audited
dispatcher selects `0x80121C6C` for backend mode 1, `0x80121D4C` for mode 2,
or performs no propagation for unsupported modes. Selected backends derive
master-scaled voice levels (and, for mode 2, pan/pitch/control-aware stereo
levels) into queued voice state; neither this owner nor the dispatcher writes
SPU registers directly.

The success return is the allocated slot index `0..23`, restored into `v0`
after dispatch. Slot-pool exhaustion and primary-voice exhaustion return
`-1`. Secondary-voice failure is nonfatal and returns the slot. The disabled
gate edge case returns the incidental `0x80170000` described above.

The three callees are therefore:

1. `0x801225F8`: allocate a sound slot;
2. `0x80121F3C`: allocate an ordinary voice, once or twice;
3. `DispatchSoundSlotUpdateToBackend`: propagate successful authoritative
   slot state to the selected queued-voice backend.

## Compact success-path equivalent

```c
int CreatePersistentSoundSlot(int resource, unsigned volume, unsigned pan,
                              unsigned pitch, int scale_q8, unsigned flags) {
    int slot_index = AllocateSoundSlot();
    SoundSlot *slot = &gSoundSlots[slot_index];
    const Resource *r = &gSoundResources[resource];

    slot->base = 0;
    slot->resource_value = r->word10;
    slot->state = 3;
    slot->class_flags = flags;
    slot->pitch_scale_q8 = scale_q8;
    slot->volume = volume;
    slot->pan = pan;
    slot->pitch = pitch;

    int primary = AllocateOrdinaryVoice();
    if (primary == -1)
        return -1; // slot remains active in the actual implementation
    InitializeOwnedVoice(primary, slot_index, r, scale_q8);

    if (flags & 8) {
        MarkPrimaryAsPaired(primary);
        int secondary = AllocateOrdinaryVoice();
        if (secondary != -1) {
            slot->secondary = secondary;
            InitializeOwnedVoice(secondary, slot_index, r, scale_q8);
        }
    } else {
        slot->secondary = -1;
    }

    DispatchSoundSlotUpdateToBackend(slot_index);
    return slot_index;
}
```

The admission gates and allocation-failure checks precede portions of this
compact success-path sketch as documented above.

## Evidence

- `disc/SCES_028.45`: authoritative instructions, nine-call census, and zero
  interior targets;
- `generated/SCES_028.45_full.ranges`: exact `0x264` owner range;
- `generated/SCES_028.45_full_33.c`: complete allocation/state/return flow;
- `docs/audits/semantic-80121534.md`: neighboring transient allocator and
  shared slot/resource/voice layouts;
- `docs/audits/semantic-8012257c.md`: final backend dispatch and queued-voice
  effects;
- `docs/audits/semantic-8013670c.md`: persistent looping-handle consumer.
