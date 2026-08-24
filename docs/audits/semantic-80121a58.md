# Semantic audit: `0x80121A58` / `ReleaseSoundSlot`

## Result and ABI

`0x80121A58` releases a live 32-byte sound slot and schedules its owned queued
voices for backend teardown.  Its evidence-backed name and ABI are:

```c
void ReleaseSoundSlot(int32_t slot_index);
```

Only `a0` is consumed.  The function is a frameless leaf and its callers never
consume `v0`; rejection and success paths leave different incidental values,
so it has no semantic return.

## Boundary and target census

The generated range assigns `[0x80121A58, 0x80121B3C)`, exactly `0xE4` bytes,
to this owner.  The preceding persistent-slot creator restores `sp` at
`0x80121A54`; this function begins independently at `0x80121A58`, returns at
`0x80121B34`, and executes a NOP delay slot at `0x80121B38`.  The next owner
starts at `0x80121B3C`.

An aligned scan of the authenticated executable finds exactly ten direct JAL
sites and no JAL to an interior instruction:

```text
80136798  80136894
80137A38  80137A44  80137A50  80137A5C
80137A68  80137A74  80137A80  80137A8C
```

The first pair replace one retained looping-sound handle with `-1` after
passing its old value in `a0`.  The remaining eight release the persistent
ambient handles rooted at `0x801E157C`.  This retained-handle use agrees with
the neighboring `CreatePersistentSoundSlot` audit.

## Validation and slot layout

The leaf returns without mutation if audio-enable byte `0x80175E68` is zero,
if `slot_index == -1`, or if its unsigned value is at least 24.  Other negative
values consequently fail the unsigned bound as well.  It does not separately
test the slot's active-state word.

Valid indices address the 32-byte record array at `0x801D674C`.  Signed bytes
`+0x18` and `+0x19` are its primary and secondary queued-voice indices.  The
routine ultimately clears the slot's word `+0x08`, the same state word set to
`3` by `CreatePersistentSoundSlot`, making the slot available to the allocator.
It deliberately leaves the resource, flags, controls, and voice-index bytes
unchanged in the inactive record.

## Owned-voice teardown

Queued voices are 36-byte records rooted at `0x801D63EC`.  For the primary
index, the routine compares voice word `+0x1C` against `slot_index`; only an
ownership match clears that voice's words `+0x00` and `+0x08` and contributes
`1 << voice_index` to a local release mask.  The active-slot invariant is that
the primary byte names a valid voice; unlike the secondary path, it is not
explicitly checked for negativity before the ownership lookup.

The secondary byte is ignored when negative.  Otherwise the same owner check,
two-word clear, and mask-bit contribution occur.  An ownership mismatch on
either voice protects a voice that has already been reassigned to another
slot.  No voice record is cleared merely because its index remains in stale
slot storage.

Finally the leaf ORs the accumulated bits into word `0x80175E9C`.  It performs
no call and writes no SPU register directly: clearing authoritative queued
state is immediate, while this global mask communicates the affected voice
indices to the later audio-backend processing path.  The mask update still
occurs with zero when neither ownership test succeeds.

## Confidence

`confirmed`: exact owner range, frameless ABI, exhaustive entry/interior JAL
census, all validation predicates, sound-slot and queued-voice strides,
ownership guards, cleared fields, slot-state release, deferred mask behavior,
and retained-handle caller role are instruction-proven.
