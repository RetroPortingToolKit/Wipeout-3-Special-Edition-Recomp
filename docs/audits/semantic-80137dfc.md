# Semantic audit: `0x80137DFC`

## Result

`0x80137DFC` is the confirmed entry of `PlaySoundEffectById`, a sound-effect
resource-selection wrapper. Its exact range is `[0x80137DFC, 0x80137ED8)`, or
`0xDC` bytes. The preceding callable ends with the independent leaf return at
`0x80137DF4..0x80137DF8`; this function has its own `0x28`-byte frame and sole
epilogue at `0x80137EC8..0x80137ED4`; the next function opens a new `0x28`-byte
frame at `0x80137ED8`. The generated range and dispatch manifests agree.

## ABI and behavior

The wrapper consumes seven arguments:

- `a0`: signed sound-effect ID;
- `a1`, `a2`, `a3`: signed 16-bit values which the wrapper ultimately passes
  to the sink as low bytes (the ubiquitous call shape strongly identifies
  `a1` as volume and `a2`/`a3` as the remaining pan/pitch controls);
- caller stack `+0x10`: a fifth sound-control value, forwarded unchanged;
- two sink-only stack arguments are synthesized by this wrapper.

ID `-1` is an explicit no-op. The wrapper clamps signed `a1` only on the upper
side (`a1 >= 231` becomes 230), then truncates `a1`, `a2`, and `a3` to eight
bits before calling `0x80121534`. It returns no meaningful value.

For ordinary IDs, `a0` selects an integer resource handle from the word table at
`0x801E178C + id*4`; the wrapper passes sink class `2` at stack `+0x14` and
selector `0` at stack `+0x18`. ID 7 instead loads the dedicated resource handle
at `0x801E17A8`, passes class `18`, and still uses selector zero.

Three IDs retain their ordinary table lookup but set a special sink selector:

| incoming ID | selector at sink stack `+0x18` |
|---:|---:|
| 29 | 23 |
| 37 | 22 |
| 28 | 21 |

Thus the complete downstream call is conceptually
`0x80121534(handle, u8(volume), u8(pan), u8(pitch), control, class, selector)`.
The sink is a larger audio-voice allocator/initializer: it rejects unavailable
audio state, obtains a free slot through `0x801225F8`, and initializes a
`0x20`-byte voice record before driving lower audio helpers. The wrapper is
therefore not merely an ID lookup; its ID 7 class and three selector remaps
choose distinct sink behavior.

## Direct-call proof

An opcode scan of the loaded main EXE finds exactly 81 direct
`jal 0x80137DFC` instructions and no additional direct call form:

```text
800E3378 800E33A0 800E3420 800E3494 800E34BC 800E3504 800E46F0
800E4750 800E8D30 80100388 80100B00 80100C30 80100D10 80100D98
80100E64 80100EA4 8010E8F0 8010EA1C 8011A340 8011C490 8011C530
8011DED0 80122FB0 801276F0 80127774 80127964 80127980 8012799C
80128850 80128898 8012893C 80128998 80128A18 80128A58 80128AE8
80128B50 80128BA8 80128C68 801290A8 80129138 801294A0 801294DC
8012960C 8012967C 801299BC 801299F8 8012A2F8 8012A364 8012A514
8012AAF4 8012AB48 8012ABBC 8012AD1C 8012AE3C 8012B03C 8012B14C
8012B188 8012B2F0 8012B350 8012B4C0 8012B568 8012B634 8012B6E4
8014C0CC 8014C1D8 8014C1FC 8014EC20 8014ECD0 8014EF88 8014F308
8014F688 80151C10 801520F8 8015225C 80152FD0 80153230 801539C0
80156B3C 80159660 80159B8C 8015BBD4
```

These sites span menu/UI, race-event, racer-effect, and gameplay owners. Their
dominant constants (`a1=255`, `a2=a3=15`, fifth argument `256`) and their use
next to racer effect creation corroborate the sound-effect interpretation.

## Evidence

- `disc/SCES_028.45`, PS-X EXE payload at file offset `0x800`, loaded at
  `0x80010000`; exact little-endian MIPS disassembly and direct-JAL opcode scan;
- `generated/SCES_028.45_full_122.c`, complete translated owner and both sink
  call paths;
- `generated/SCES_028.45_full_115.c`, translated `0x80121534` sink;
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_dispatch.c`, exact range and continuation entries.
