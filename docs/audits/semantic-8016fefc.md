# Semantic audit: guest `0x8016FEFC`

## Result

`0x8016FEFC` is a real address-taken renderer callback with a private register
ABI.  It is the complementary sibling of `0x8016FD34`: both reject a quad by
its common viewport outcode, send near geometry to the shared clipper, and
otherwise emit three 32-byte textured triangles.  This variant creates and
projects the midpoint of the **first** input edge (`$t6`--`$t7`), so the
descriptive name is:

```text
SubmitTexturedQuadFirstEdgeFan
```

The name describes observed geometry and does not claim recovery of an
original source identifier.

## Boundary and references

- `DispatchPackedFacePrimitive` materializes `0x8016FEFC` with the
  `lui $t2,0x8016` / `ori $t2,$t2,0xFEFC` pair at
  `0x8016FC38..0x8016FC3C`.  Descriptor selectors 5 and 7 install it as
  scratchpad callback 0.
- `ProjectAndSubmitPackedFaceGroups` (`0x8016F94C`) and
  `SubdivideAndSubmitPackedFaceStrip` (`0x8016FA70`) load callback addresses
  from that scratchpad table and invoke them indirectly.  Thus the synthesized
  pointer is stronger boundary evidence than a generated AOT range split.
- The callback's reject path owns `jr $ra` at `0x8016FF20`; its accepted path
  begins at `0x8016FF28`; its direct packet path owns `jr $ra` at
  `0x801700BC`, with the packet-cursor advance in the delay slot.
- The near path at `0x8016FF5C` tail-transfers to `0x80170B5C`, which preserves
  the inherited return continuation.  Consequently the semantic function is
  `0x8016FEFC..0x801700C0`.  Generated `func_8016FF28` is an internal
  control-flow/root split, not a second ABI entry.

No direct `j`/`jal` targets this address.  That is expected for a callback;
the dispatcher materialization and later `jalr` calls establish the reference.
The physically preceding sibling ends independently at `0x8016FEF8`, and the
following `SubmitTexturedQuad` begins independently at `0x801700C4`.

## Private register contract

The live contract matches the adjacent callback family and is not normal o32:

| Register/state | Observed role |
|---|---|
| `$t6`, `$t7`, `$t8`, `$t9` | four projected-vertex records: signed XYZ at `+0/+2/+4`, packed SXY at `+8`, unsigned depth at `+12`, clip flags at `+14` |
| `$v0` | texture/template words used by the three packet builders |
| `$t4` | face/material record; unaligned data at `+18` supplies color/command bytes |
| `$s2` | ordering-table base |
| `$s4` | packet cursor, advanced by `0x60` only on the direct path |
| `$gp` | temporary five-component midpoint/projection record |
| `$fp`, `$ra` | inherited renderer context and return continuation |

There is no scalar result.  Effects are packet writes, ordering-table links,
and the updated packet cursor.  `emit = false` is required until this whole
register-ABI renderer family has a structured adapter.

## Culling and near clipping

At `0x8016FEFC..0x8016FF14`, the callback loads the signed clip-flag halfword
at `+14` from all four vertices and ANDs the values.  A nonzero common flag
means all vertices are beyond the same viewport plane; it returns without
allocating a packet.  The surviving edge loads all four depths, chooses an OT
bucket from `(z0 + z1 + z2 + z3) >> 7`, and tests every depth against 125.

If any vertex is nearer than 125, control tail-transfers to
`ClipAndSubmitNearTexturedQuad` at `0x80170B5C`.  That is the same near-plane
path used by `0x8016FD34` and `SubmitTexturedQuad` (`0x801700C4`).

## Direct three-triangle path

When every depth is at least 125, `0x8016FF64..0x801700C0`:

1. averages the signed XYZ components of `$t6` and `$t7` into the `$gp`
   scratch record and projects that first-edge midpoint with GTE `RTPS`;
2. builds three adjacent 32-byte textured-triangle packets at
   `$s4 + {0,32,64}`, combining the new projected midpoint with the original
   four SXY records;
3. copies the texture, CLUT, TPAGE, and face command/color fields into the
   three packets and links their tags into the selected ordering-table bucket;
4. returns through the inherited `$ra`, advancing `$s4` by 96 bytes in the
   delay slot.

`SubmitTexturedQuadTriangleFan` at `0x8016FD34` has the same cull, depth,
clipper, packet-count, and cursor contract, but midpoint-projects `$t8`--`$t9`
instead.  `SubmitTexturedQuad` at `0x801700C4` does not add a midpoint: its
far path emits one 40-byte `POLY_FT4`.  The callback matrix therefore selects
between an unsplit quad and two complementary edge-split, three-triangle
forms.

## Symbol decision

Add `0x8016FEFC` as `[[func]] SubmitTexturedQuadFirstEdgeFan`, `status =
"known"`, and `emit = false`.  The dispatcher takes its address and the body
owns return behavior, so representing it as `[[block]]` would erase a genuine
semantic boundary.  `known` records that the behavior and boundary are
instruction-level facts while the source spelling remains unknown.

## Evidence inspected

- `generated/SCES_028.45_full_140.c`: dispatcher constant construction,
  indirect callback family, and control/data flow through
  `0x8016FEFC..0x801700C0`.
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_dispatch.c`: generated range/continuation ownership,
  used only as navigation evidence.
- `docs/audits/semantic-8016fc28.md`,
  `docs/audits/semantic-8016fd34.md`,
  `docs/audits/semantic-801700c4.md`, and
  `docs/audits/semantic-80170b5c.md`: independently audited dispatcher and
  sibling callback contracts.
