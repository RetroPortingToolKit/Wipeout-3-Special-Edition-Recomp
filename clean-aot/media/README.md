# Clean AOT media layer

This directory is the user-media boundary for the clean static-recompilation
lane. It is a bounded C++20 library, not an emulator, disc filesystem, or
runtime code translator. It owns only the parts that must turn a player-owned
Redump-style raw BIN/CUE into deterministic sectors and audio samples. The
interface has no dependency on `clean-aot/cdrom/**`; a later integration change
can adapt `CueImage` to the accepted CD-ROM controller without changing the
parser or decoder.

## Contract

`CueImage::open` accepts a CUE text file with:

- `FILE "..." BINARY` entries resolved relative to the CUE directory;
- `TRACK nn MODE1/2352`, `TRACK nn MODE2/2352`, and `TRACK nn AUDIO` tracks;
- `INDEX 00`, `INDEX 01`, `PREGAP`, and `POSTGAP` in valid command order;
- the normal Redump metadata directives (`REM`, `CATALOG`, `TITLE`,
  `PERFORMER`, `SONGWRITER`, `ISRC`, `FLAGS`, and `CDTEXTFILE`) as inert
  metadata.

All other file types, track modes, index numbers, unknown directives, malformed
MSF values, non-contiguous track numbers, path traversal, absolute paths,
symlinks that leave the CUE directory, non-raw file sizes, and arithmetic that
would leave the bounded disc coordinate space fail closed. A CUE is bounded to
8 MiB, each line to 4096 bytes, and a raw image to 449,850 sectors. No image
bytes are loaded while parsing. A raw read opens the referenced BINARY and
reads exactly one 2352-byte sector at the computed offset.

Logical LBA 0 is track 01 INDEX 01 at MSF 00:02:00. Track 01 hidden lead-in
sectors before that point are deliberately not addressable. An INDEX 00 is
stored media belonging to its track. A PREGAP without INDEX 00 is a bounded
synthetic zero sector range before INDEX 01; POSTGAP is a synthetic zero range
after the stored track. These logical gaps do not require a shared logical/file
offset base, so the same BINARY can contain stored tracks separated by
synthetic gaps. `TrackInfo` exposes both logical and stored boundaries, whether
each directive was present, the exact relative MSF values, and the file-sector
range, so the CD-ROM layer does not have to reconstruct CUE semantics.

`read_mode1_sector` validates the sync pattern, expected BCD MSF, mode byte,
and the Mode 1 EDC over the complete sync/header/payload prefix before exposing
the 2,048-byte payload. `read_mode2_sector` validates the sync pattern, expected BCD MSF, mode byte,
duplicated XA subheader, and EDC. It exposes the exact 8-byte subheader and
either the 2048-byte Form 1 or 2324-byte Form 2 payload. Form 2 accepts the
standard optional zero EDC. `read_cdda_sector` exposes 588 signed stereo frames
at 44.1 kHz. For raw PSX/Redump BIN tracks, the disc image convention is
interleaved signed 16-bit little-endian PCM: left low byte, left high byte,
right low byte, right high byte. There is no byte-order guessing.

`read_xa_sector` requires an AUDIO + FORM2 Mode 2 sector, preserves its file,
channel, submode, and coding-info metadata, and exposes the first 2304 bytes
of XA data. `XaAdpcmDecoder` handles the public PS1 XA 4-bit and 8-bit layouts,
mono or stereo, and 37.8/18.9 kHz. Predictor history is bounded to two values
per channel and is carried between sectors until `reset` is called. Emphasis is
reported in the format but rejected by the decoder because its PS1 formula is
not specified by the public references and is not used by known PS1 software.

`XaRateConverter` is a deterministic host boundary from either XA source rate
to 44.1 kHz. It uses a rational phase/carry and two-sample linear interpolation;
it never accumulates floating-point time or an unbounded stream buffer. For
complete XA sector-sized blocks the output count is exact (`37800 -> 44100` is
7/6 and `18900 -> 44100` is 7/3). This is an explicit deterministic conversion
policy, not a claim that the undocumented drive-side 25-tap interpolation has
been reproduced.

## Build and test

Run this component independently:

```text
cmake -S clean-aot/media -B build-clean-media -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-clean-media --parallel
ctest --test-dir build-clean-media --output-on-failure
```

The library and tests use C++20 and promote conversion, sign-conversion,
shadowing, pedantic, and general warnings to errors under GCC and Clang. A
sanitizer build uses a separate directory:

```text
cmake -S clean-aot/media -B build-clean-media-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCLEAN_AOT_MEDIA_ENABLE_SANITIZERS=ON
cmake --build build-clean-media-asan --parallel
ctest --test-dir build-clean-media-asan --output-on-failure
```

The tests create only synthetic sectors in a temporary directory. No user
disc, BIOS, title-specific media, or media hash is part of this source tree.
