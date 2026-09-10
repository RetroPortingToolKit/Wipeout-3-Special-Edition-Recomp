# Semantic audit: guest `0x8016E170`

## Result

`0x8016E170` is **data, not a callback or function boundary**. It is the first
byte of the 29-byte NUL-terminated string:

```text
c:\wipeout3\common\shads.bt4
```

The defensible symbol is the confirmed object `kShadsBt4SourcePath`. It must
not be added as `[[func]]` or `[[block]]`, and there is no callable ABI to
recover at this address. This corrects the earlier interpretation that
`InitializeRaceRuntimeResources` registers a callback here.

## Exact boundary and bytes

The authenticated PS-X EXE maps file offset
`0x800 + (0x8016E170 - 0x80010000) = 0x15E970` to guest `0x8016E170`.
The bytes are:

```text
8016E170  63 3A 5C 77 69 70 65 6F 75 74 33 5C 63 6F 6D 6D  c:\wipeout3\comm
8016E180  6F 6E 5C 73 68 61 64 73 2E 62 74 34 00           on\shads.bt4.
```

The object occupies `0x8016E170..0x8016E18C` inclusive. Alignment padding at
`0x8016E18D..0x8016E18F` is zero; the next non-string table begins at
`0x8016E190`. The preceding string, `About to Start Race`, ends at
`0x8016E16F`, independently fixing the start boundary.

Interpreting the first word as MIPS yields an implausible `jalx` from ASCII
bytes (`63 3A 5C 77`). No branch, jump, indirect dispatch entry, prologue,
epilogue, or generated basic-block label owns `0x8016E170`.

## Direct reference and dataflow

There is one retained address construction:

```text
80124928  lui    a0, 0x8017
8012492C  addiu  a0, a0, -0x1E90       # a0 = 0x8016E170
80124930  jal    0x80141D6C
80124934  addiu  a1, zero, 1           # delay slot
```

This passes the string pointer as ordinary `$a0`; it does not take an
executable address. `0x80141D6C` immediately replaces `$a0` with the arena
pointer loaded from `0x8018F098` before its first call and never reads, saves,
compares, or forwards the incoming `$a0`. Thus the path argument is unused in
this retained binary. `$a1 = 1` is preserved and contributes to descriptor
flag fields.

The callee allocates 38 bytes through `0x8011686C`, initializes a halfword-
oriented display/resource descriptor, calls `0x800EEC80` with a six-halfword
stack record and the global at `0x8003484C`, and returns the descriptor pointer
in `$v0`. The caller publishes it at `0x801879E0`. None of those operations
invokes `0x8016E170` or reads its bytes.

The likely historical intent of a `.bt4` path is not needed for the
classification. `kShadsBt4SourcePath` preserves the exact leaf name without
claiming an unrecovered asset format or original C identifier.

## Generated-range anomaly

`generated/SCES_028.45_full.ranges` contains the broad seeded range
`R 8016CC0C 2574`, which numerically spans this address. That is not evidence
that every byte is executable: this string and neighboring UI strings/tables
prove the seed coalesced code and embedded data. There is no `block_8016E170`,
dispatch entry, or declaration in the generated AOT output. Future coverage
accounting must split or subtract this data island rather than count it as
decompiled instructions.

## Symbol decision

Add a `[[object]]` at `0x8016E170` named `kShadsBt4SourcePath`, with `status =
"confirmed"`. Confirmed is justified for the address, extent, contents, sole
direct reference, and non-code classification. The name is descriptive rather
than recovered but asserts no more than the literal data proves.

## Evidence inspected

- `disc/SCES_028.45`: payload bytes at file offset `0x15E970`.
- `generated/SCES_028.45_full_117.c`: address construction and call at
  `0x80124928..0x80124934`.
- `generated/SCES_028.45_full_126.c`: complete `0x80141D6C..0x80141E5B`
  dataflow proving incoming `$a0` is unused and `$v0` is a newly allocated
  38-byte descriptor.
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_dispatch.c`: broad range anomaly and absence of an
  entry at `0x8016E170`.
