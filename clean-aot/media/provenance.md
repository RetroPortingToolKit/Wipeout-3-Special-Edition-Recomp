# Media provenance

The implementation in this directory is independently authored. It does not
copy source, tables, tests, or generated output from DuckStation, GooseStation,
Mednafen, or any other emulator. The references below were used only for public
format field meanings and arithmetic invariants; the code, tests, error model,
file I/O boundary, and conversion state machine were written for this
static-recompilation repository.

## Public references

- [ECMA-130, Data interchange on read-only 120 mm optical data disks](https://www.ecma-international.org/wp-content/uploads/ECMA-130_2nd_edition_june_1996.pdf)
  defines 2352-byte digital sectors, the sync/header organization, EDC/ECC
  boundaries, and the relationship between data and audio tracks.
- [Sony Psy-Q File Formats developer reference](https://psx.arthus.net/sdk/Psy-Q/DOCS/Devrefs/Filefrmt.pdf)
  describes the PlayStation’s supported CD-ROM XA audio rates, channel modes,
  and the fact that XA audio is delivered from the CD-ROM decoder to the SPU.
- [PSX-SPX public specification, CD-ROM sector encoding](https://static.grumpycoder.net/pixel/document.pdf)
  was consulted for the public Mode 2/Form 1 and Form 2 offsets, duplicated
  subheader, optional Form 2 EDC, raw CDDA byte order used by PSX BIN images,
  XA coding-info bits, 128-byte sound-group layout, filter coefficients, and
  the 4-bit/8-bit sample expansion rules.
- [libyal CUE sheet format working document](https://github.com/libyal/libodraw/blob/main/documentation/CUE%20sheet%20format.asciidoc)
  was consulted for the text grammar and the ordering/meaning of `INDEX 00`,
  `INDEX 01`, `PREGAP`, and `POSTGAP`. It is treated as a format description,
  not as an implementation dependency.

## Deliberate policy decisions

The CUE reader accepts only the raw `BINARY` form needed by player-supplied
Redump BIN/CUE. It does not silently decode WAVE/MP3, merge files, infer
missing gaps, auto-detect audio byte order, or follow arbitrary paths. A
synthetic gap is exposed as zero raw bytes for CDDA reads but is rejected by
the Mode 2/XA readers because it has no sector header or payload.

The XA decoder uses the public PS1 four-filter coefficient set and bounded
per-channel predictor state. The rate converter intentionally uses exact
rational phase/carry with integer interpolation rather than copying an
emulator’s implementation or claiming undocumented CD-ROM interpolation
behavior. Emphasis is surfaced but rejected at the decode boundary because
the public PSX documentation leaves its formula unspecified and notes that it
is not used by known PlayStation software.

