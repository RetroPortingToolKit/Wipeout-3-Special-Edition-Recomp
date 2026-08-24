# Semantic audit: `0x80136D0C`

## Result

`0x80136D0C` is the confirmed entry of
`PlaySpatialSoundAtSegmentMidpoint`. Its exact range is
`[0x80136D0C, 0x80136F58)`, or `0x24C` bytes. The preceding owner has an
independent epilogue at `0x80136CDC..0x80136D08`; this routine opens its own
`0x50`-byte frame, has one return epilogue at `0x80136F34..0x80136F54`, and is
immediately followed by a distinct `0x70`-byte-frame prologue at `0x80136F58`.
The generated range and dispatch manifests record the same boundary.

## ABI

The adapter receives:

- `a0`: signed sound-resource index;
- `a1`: signed control offset, forwarded to the sink as `a1 + 0x100`;
- `a2`: ignored (callers commonly supply 20, 30, or 40, but no instruction in
  the callee reads it);
- `a3`, stack `+0x10`, stack `+0x14`: explicit source X/Y/Z;
- stack `+0x18`: also ignored, despite callers consistently populating it;
- stack `+0x1C`: orientation/context record;
- signed halfword at stack `+0x20`: attenuation scale.

The context record's first word points to a structure whose `+0x14/+0x18/+0x1C`
words provide the other endpoint. Its own unsigned halfword at `+8` provides
the reference heading.

## Midpoint, angle, and attenuation math

The routine averages each explicit coordinate with the linked endpoint using
signed arithmetic shifts. It sends the resulting XYZ midpoint to `0x8014B614`.
That helper's scalar result is subtracted from `0x4000` and floored at zero; if
the resulting attenuation is zero, the routine returns without playing sound.

For a live sound it computes the midpoint's horizontal angle through
`0x8015C518(midX, midZ)`, subtracts that from `context->heading`, and normalizes
the signed 16-bit difference through `0x8014B6C0`. The adapter then explicitly
wraps the result into the interval `[-0x400, +0x400]` by subtracting or adding
`0x800` when necessary.

Starting with equal left/right attenuation `A = max(0x4000-distance, 0)`, it
applies the relative angle as:

```text
left  = A + ((A * angle) >> 11)
right = A - ((A * angle) >> 11)
left  = (left  * scale) >> 19
right = (right * scale) >> 19
```

Volume is `(left + right) >> 1`, clamped only above at 230. Pan defaults to
center 15. When both channels are positive, it computes a left-channel ratio,
maps it into `[-15,+15]`, clamps that intermediate value to `[0,30]`, and
passes `30 - value`; equivalently the ordinary range is a clamped 0..30 pan.

## Sink arguments

The final call is exactly:

```text
0x80121534(
    *(0x801E178C + signed_sound_id * 4),
    u8(volume),
    u8(pan),
    15,
    signed_control + 0x100,
    0,
    0)
```

Thus this routine shares the same resource table and downstream voice sink as
`PlaySoundEffectById`, but derives volume and stereo position from geometry.
No meaningful return value is established.

## Caller proof

An opcode scan of the loaded EXE finds exactly 34 direct
`jal 0x80136D0C` instructions:

```text
800EAE78 800EAEE8 800EB3A4 800EB414 800EBBC4 800EBC40 800EC71C
800EC78C 8011F10C 8011F184 8014E284 8014E2F4 80150C4C 80150CBC
80150DE0 80150E50 80150F00 80150F70 80151EE0 80151F50 801543D8
80154448 80155B44 80155BB4 80155D58 80155DC8 801595D0 80159640
8015986C 801598DC 8015AD80 8015ADEC 8015AF88 8015AFF8
```

Representative call sites explicitly populate stack `+0x10..+0x20` and pass
the context pointer at `+0x1C`, corroborating the nonstandard nine-argument
ABI (including its two ignored slots). The paired calls in gameplay owners also switch between current-player
contexts while preserving the same source endpoint and attenuation scale,
which supports the spatial-audio interpretation.

## Evidence

- `disc/SCES_028.45`, PS-X EXE payload loaded at `0x80010000`, exact
  little-endian MIPS disassembly and direct-JAL opcode scan;
- `generated/SCES_028.45_full_121.c`, complete translated routine;
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_dispatch.c`, boundary and continuation entries;
- representative direct callers at `0x8011F10C`, `0x8014E284`,
  `0x80150C4C`, and `0x801595D0`.
