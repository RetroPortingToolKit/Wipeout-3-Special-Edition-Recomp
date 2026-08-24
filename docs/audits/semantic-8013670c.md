# Semantic audit: `0x8013670C`

## Result

The callable at `0x8013670C` is the confirmed six-argument dispatcher
`UpdateLoopingSoundChannel`. It owns a two-dimensional table of persistent
sound handles and volume ramps indexed by a trusted sound selector and channel
selector. It stops/reset channels, creates a missing looping sound, or updates
four parameters on an existing sound.

Its recovered call shape is:

```c
void UpdateLoopingSoundChannel(
    int selector,            /* a0 */
    int channel,             /* a1 */
    int pitch_scale_q8,      /* a2 */
    uint8_t pitch,           /* a3 */
    uint8_t pan,             /* stack +0x10 */
    int16_t volume_target);  /* stack +0x14 */
```

The dominant callers pass `pitch_scale_q8=0x100`, `pitch=15`, and channel 0
or 1, while varying pan and volume target. The downstream sound-instance field
writes independently prove the pan, pitch, scale, and volume assignments.

## Exact boundary and ABI

- `0x8013670C` opens a unique 56-byte frame and saves `ra` plus `s0..s5`.
- It preserves `a2`, truncates `a3` to a byte, loads the fifth argument as an
  unsigned byte from caller stack `+0x10`, and the sixth as a signed halfword
  from caller stack `+0x14`.
- Every branch joins the restores at `0x80136998`. The return is `jr ra` at
  `0x801369B4`, with frame restoration in its delay slot at `0x801369B8`.
- The exact range is therefore `0x8013670C..0x801369BB`, `0x2B0` bytes. A
  different function opens its own 80-byte frame at `0x801369BC`.
- No path defines a public return value; all observable results are table and
  audio-instance mutations.

## Table indexing and validation

For selector `s` and channel `c`, the dispatcher indexes:

| Address | Element | Index expression |
|---|---|---|
| `0x801E14FC` | signed 16-bit target volume | `2*c + 4*s` |
| `0x801E188C` | signed 16-bit current volume | `2*c + 4*s` |
| `0x801E157C` | signed 32-bit sound handle | `4*c + 8*s` |
| `0x801E168C` | selector metadata `{sound_id, volume_q13}` | `8*s` |
| `0x801E178C` | sound-resource handle table | `4*sound_id` |

The selector and channel are trusted for the state-table accesses; there is no
up-front bounds check. Only missing-handle resource creation validates the
selector: unsigned `(s - 4) < 28`, exactly selectors `4..31`. Outside that
range the fallback sound ID is forced to `-1`, so no sound is created. For a
valid selector, metadata word `+0` must also be other than `-1` before the
resource table is dereferenced.

The audio-status gate at `0x801D1B3C` is narrower than a general enable check:
the update returns early only when byte `+2 == 1` and byte `+0 == 0`. If byte
`+2 != 1`, or if byte `+0` is nonzero, normal processing continues.

## State/action matrix

### Explicit stop/reset: `volume_target == -1`

The dispatcher clears both target and current volume. It reads the existing
handle; if it is not `-1`, it stores `-1` into the handle slot and calls
`0x80121A58(old_handle)` to stop/release that sound. An already empty slot is a
no-op.

### Ramp and zero-stop

For every other target, the signed halfword target is stored. Target `255`
also snaps current volume to `255` before comparison. Otherwise current volume
moves toward target by at most 16 each call:

```c
if (target - current >= 17)      current += 16;
else if (current - target >= 17) current -= 16;
else                             current = target;
```

If the resulting current volume is zero, an existing sound is stopped through
`0x80121A58` and its handle slot is cleared to `-1`. An empty slot remains
empty.

### Create missing sound

For nonzero current volume, the effective byte volume is:

```c
volume = (current * metadata[selector].volume_q13) >> 13;
```

When the handle slot is `-1`, valid selector/resource metadata is required as
described above. The dispatcher calls `0x801217F4` with:

```text
resource_table[sound_id], u8(volume), u8(pan), u8(pitch),
pitch_scale_q8, class/flags 0
```

and stores the returned persistent sound handle in the `(selector,channel)`
slot.

### Update existing sound

For a live handle it makes four ordered updates:

| Callee | Proven sound-instance mutation | Value |
|---|---|---|
| `0x801223EC` | byte `+0x1B` | `pan` |
| `0x80122450` | byte `+0x1C` | `pitch` |
| `0x80121B3C` | word `+0x10`, propagated to owned voices | `pitch_scale_q8` |
| `0x801224B4` | byte `+0x1A` | scaled `volume` |

These are the same sound-instance fields established independently for the
shared audio sink at `0x80121534`, making the looping-sound role explicit.

## Complete direct callers

An aligned opcode scan of the retained executable finds exactly 28 calls:

```text
800F0164 800F01F0 80136C98 80136CC0 801374A0 801378CC 801378E8
801379BC 801379D8 80137AA8 80137AC4 8014BD58 8014BD98 8014BEFC
8014C054 80152184 801521D0 801524EC 80152750 80153060 801532CC
80153638 80153A4C 80156C8C 80156CDC 80156D18 8015BAF8 8015BC60
```

The sites include paired channel-0/channel-1 updates, explicit `-1` stops,
zero/`0xFE`/`0xFF` ramp targets, and selectors 4, 6, and 7 plus caller-derived
selectors. This diversity matches a shared persistent-sound dispatcher rather
than a one-effect wrapper. No direct call targets an interior block.

## Catalog decision

`0x8013670C` is `UpdateLoopingSoundChannel`, `status="confirmed"`,
`emit=false`. The function is semantically complete, but it remains guest-owned
because it mutates shared title/audio state and calls several guest audio
helpers.

## Sources checked

- `disc/SCES_028.45`: exact disassembly and complete JAL census.
- `generated/SCES_028.45_full_121.c`: complete dispatcher.
- `generated/SCES_028.45_full_115.c`: create/stop/update helper mutations.
- `generated/SCES_028.45_full.ranges`: exact `0x2B0` owner range.
- `docs/audits/semantic-80121534.md`: independently recovered shared
  sound-instance layout and volume/pan/pitch/pitch-scale fields.
