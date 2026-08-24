# Semantic audit: guest `0x80170D3C`

## Result

`0x80170D3C` is a genuine no-argument renderer setup leaf spanning exactly
`0x80170D3C..0x80170E03` (`0xC8` bytes).  Its best-supported name is
`InitializeSceneryClipScratchpad`.

The routine seeds a 17-word scratchpad record from two adjacent immutable
renderer template blocks, then initializes three fields in the global scenery
clipping workspace.  It does not project geometry or emit GPU packets.

## Boundary and caller

The generated range manifest records `R 80170D3C C8`; the next independent
entry is `0x80170E04`.  The body is one straight-line basic block.  It ends with
`jr ra` at `0x80170DFC` and a NOP delay slot at `0x80170E00`.  There is no stack
frame, branch, or nested call.

The complete generated corpus contains one direct call target to this entry:
`SubmitSceneSectionFaces` calls it at `0x80142578` and resumes at
`0x80142580`.  This establishes both its callable boundary and its role as
setup immediately before the section renderer continues.

## Demonstrated writes

The first copy uses eight consecutive words:

- source `0x801762E0..0x801762FC`;
- destination `0x1F800078..0x1F800094`.

The second copy uses nine consecutive words:

- source `0x80176300..0x80176320`;
- destination `0x1F800098..0x1F8000B8`.

Together these are one contiguous 68-byte scratchpad record.  The explicit
unrolled loads and stores prove the extent; no caller arguments participate.

Finally, the routine writes the clipping workspace based at `0x801E25D0`:

| Address | Width | Value |
|---|---:|---:|
| `0x801E25D0` | word | `2` |
| `0x801E25E0` | word | `20` |
| `0x801E2620` | halfword | `3` |

These addresses are the same workspace family used by the audited scenery
clipper and its renderer adapters.  The values are fixed initialization state,
not a scalar return contract.  The function returns only through `ra`; all
meaningful results are its scratchpad and workspace side effects.

## Evidence

- `generated/SCES_028.45_full.ranges`: exact `0xC8` extent and the next root at
  `0x80170E04`.
- `generated/SCES_028.45_full_138.c`: every source load, scratchpad store,
  workspace write, and the terminal `jr ra`.
- `generated/SCES_028.45_full_125.c`: sole direct call at `0x80142578` inside
  `SubmitSceneSectionFaces`, returning to `0x80142580`.
- `docs/audits/semantic-801422fc.md`: independently established owner routine.
- `docs/audits/semantic-80170b5c.md` and
  `docs/audits/semantic-clip-globals.md`: independently established clipping
  workspace identity and renderer use.
