# Semantic audit: guest `0x8016FC28`

## Result

`0x8016FC28` is a real callable entry and the sole direct callee used for the
first indexed-face list in `SubmitSceneSectionFaces`.  It is not itself a
polygon packet builder.  The defensible name is:

```text
DispatchPackedFacePrimitive
```

The routine decodes a 16-bit packed face descriptor, installs four
renderer-private callback addresses in scratchpad, and tail-jumps to one of
three primitive-rendering families.  Packet construction, GTE projection,
clipping, and ordering-table linkage happen in those downstream routines.

## Boundary and caller evidence

- The only direct branch to the entry in the base image is the `jal` at
  `0x801423D0`; its delay slot copies the signed 16-bit list entry into `$a1`
  and its return PC is `0x801423D8`.
- The instruction before the entry is the return delay slot of the preceding
  routine at `0x8016FC20`/`0x8016FC24`.
- The dispatcher itself occupies `0x8016FC28..0x8016FD30` (268 bytes).  Its
  final instruction at `0x8016FD30` is the delay slot of `jr $t4` at
  `0x8016FD2C`.
- `0x8016FD34` is not a fallthrough continuation of the dispatcher.  Its
  address is deliberately synthesized into the callback table, and it can
  return independently or continue into the separately rooted renderer at
  `0x8016FD60`.  The generated base AOT range of 312 bytes merges this callback
  entry into `func_8016FC28`; that range is useful dispatch coverage but is not
  the true source-function boundary.
- No address-bearing data word targets `0x8016FC28`.  The one static `jal` is
  therefore the complete direct-call evidence in the inspected base image;
  an unloaded overlay could still call it indirectly.

The sole caller passes the section owner loaded from its render context in
`$a0` and one sign-extended face-list halfword in `$a1`.  The dispatcher does
not dereference `$a0`; it preserves it for the selected renderer.

## Packed descriptor decode

The entry splits `$a1` without any table reads:

| Packed bits | Downstream value |
| --- | --- |
| `0..9` | `$a1 = descriptor & 0x03ff`, the face/record index |
| `10..12` | `$a2 = ((descriptor >> 10) & 7) + 1`, a count in `1..8` |
| `13..15` | eight-way dispatch selector |

Calling the middle field a downstream count is intentionally conservative.
The renderer families use it to control their projected-record work, but this
audit does not claim an original source typedef or whether every format names
it a vertex, edge, or pair count.

## Scratchpad callback matrix and tail dispatch

Before selecting a renderer the routine prepares these defaults:

```text
$t0 = 0x801700C4    $t1 = 0x801700C4
$t2 = 0x8016FEFC    $t3 = 0x8016FD34
$t4 = 0x8016F94C    $t5 = 0x8016FA70
$t6 = 0x80170774
```

The selector changes `$t0`, `$t1`, and/or the final target `$t4`.  It then
writes `{ $t0, $t1, 0x801700C4, 0x801700C4 }` to
`0x1F800000..0x1F80000F`, leaves `$at = 0x1F800010`, and executes `jr $t4`.
The exact matrix is:

| Selector | Callback 0 | Callback 1 | Renderer tail target |
| ---: | --- | --- | --- |
| 0 | `0x801700C4` | `0x801700C4` | `0x8016F94C` |
| 1 | `0x801700C4` | `0x801700C4` | `0x8016F94C` |
| 2 | `0x801700C4` | `0x801700C4` | `0x8016FA70` |
| 3 | `0x801700C4` | `0x801700C4` | `0x80170774` |
| 4 | `0x801700C4` | `0x8016FD34` | `0x8016FA70` |
| 5 | `0x8016FEFC` | `0x801700C4` | `0x8016FA70` |
| 6 | `0x8016FD34` | `0x801700C4` | `0x8016F94C` |
| 7 | `0x8016FEFC` | `0x801700C4` | `0x8016F94C` |

Selectors 0 and 1 are observably identical in this dispatcher.  Any semantic
difference encoded by that high bit is not consumed here and should not be
invented in a lift.

## ABI and packet behavior

At its normal entry the routine has a small o32-facing contract:

```text
void DispatchPackedFacePrimitive(
    void *section_owner,       /* $a0, forwarded unchanged */
    uint16_t packed_face);     /* $a1, decoded as above */
```

It is implemented as a tail dispatcher rather than an ordinary returning C
function.  The selected renderer inherits the original `$ra`, `$a0`, decoded
`$a1/$a2`, the callback table, and the remaining private register state.  A
future structured lift therefore needs a renderer adapter or must lift this
whole family together; `emit = false` remains appropriate.

The dispatcher performs no GTE command, allocates no GPU packet, advances no
packet cursor, and writes no ordering-table entry.  Its only stores are the
four scratchpad callback words.  `0x801700C4` is the known
`SubmitTexturedQuad` callback: it trivial-rejects four projected vertices,
emits and OT-links one 40-byte `POLY_FT4` on the far path, or transfers to the
shared near clipper.  `0x8016FEFC` has the same callable-shaped four-vertex
outcode wrapper with a sibling packet path, while `0x8016FD34` is another
callback entry that performs four-vertex common-outcode rejection before its
accepted path.  Those callbacks explain the relationship to the quad
submitters without attributing their packet side effects to this dispatcher.

## Symbol decision

- Add `0x8016FC28` as `[[func]] DispatchPackedFacePrimitive`.
- Mark it `known`: entry, sole direct caller, packed fields, callback matrix,
  tail targets, and absence of packet writes are all instruction-level facts.
- Keep `emit = false`: its observable contract includes a scratchpad callback
  table and a private-register tail transfer, not a standalone host ABI.

## Evidence inspected

- `disc/SCES_028.45`: direct `j`/`jal` and address-word scan.
- `generated/SCES_028.45_full_126.c`: caller at
  `0x801423C4..0x801423D8`.
- `generated/SCES_028.45_full_140.c`: dispatch instructions at
  `0x8016FC28..0x8016FD30` and the adjacent callback at `0x8016FD34`.
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_dispatch.c`: generated roots and continuation
  ownership, used with the boundary correction above.
- `generated/overlays_static.c`: captured implementation rooted at
  `0x8016F94C`, including calls into the shared projection/quad family.
- `docs/audits/semantic-801422fc.md`,
  `docs/audits/semantic-801700c4.md`, and
  `docs/audits/semantic-80170254.md`: surrounding independently audited
  renderer contracts.
