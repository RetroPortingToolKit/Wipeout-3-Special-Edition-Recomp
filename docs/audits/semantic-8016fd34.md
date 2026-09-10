# Semantic audit: guest `0x8016FD34`

## Result

`0x8016FD34` is a real callback entry with a renderer-private register ABI.
It is not fallthrough from `DispatchPackedFacePrimitive`; that dispatcher
ends at `0x8016FD30`, the delay slot of its indirect tail jump.  The
defensible name is:

```text
SubmitTexturedQuadTriangleFan
```

The callback trivial-rejects a four-vertex primitive, sends near geometry to
the shared textured-quad clipper, and otherwise emits a three-triangle fan.
The far path creates one projected midpoint on the edge between the third and
fourth input vertices, then writes and ordering-table-links three consecutive
32-byte textured-triangle packets.

## Boundary and reference evidence

- `DispatchPackedFacePrimitive` synthesizes `0x8016FD34` with the
  `lui $t3,0x8016` / `ori $t3,$t3,0xFD34` pair at
  `0x8016FC40..0x8016FC44`.  Descriptor selectors 4 and 6 install that value
  into scratchpad callback slots 1 and 0 respectively.
- The primitive renderer at `0x8016FA70` loads those callback words from
  scratchpad and invokes them through four `jalr` sites at `0x8016FB6C`,
  `0x8016FB84`, `0x8016FB9C`, and `0x8016FBB4`, after arranging a different
  four-record vertex tuple for each call.
- No direct `j`/`jal` targets `0x8016FD34`, and the base executable contains
  no literal address word for it.  The synthesized callback assignment is
  therefore the concrete address-taken reference.
- The callback's reject edge executes `jr $ra` at `0x8016FD58`.  Its accepted
  edge branches to `0x8016FD60`, and the far implementation owns a second
  `jr $ra` at `0x8016FEF4`.  The near edge at `0x8016FD94` tail-transfers to
  `0x80170B5C`, which preserves the inherited return address.
- Consequently the semantic function spans `0x8016FD34..0x8016FEF8`, with
  `0x8016FD60` as its accepted continuation.  The generated AOT range split
  at `0x8016FD60` is a control-flow/root split, not evidence of an independent
  o32 entry.

## Private register contract

This is not safely expressible as a normal four-argument C function.  Its
demonstrated live inputs are:

| Register/state | Meaning |
|---|---|
| `$t6`, `$t7`, `$t8`, `$t9` | four projected-vertex records; signed XYZ at `+0/+2/+4`, packed SXY at `+8`, unsigned depth at `+12`, clip flags at `+14` |
| `$v0` | texture/template words consumed by the packet builder |
| `$t4` | face/material record supplying packed command/color data |
| `$s2` | ordering-table base |
| `$s4` | packet cursor, advanced by 96 bytes on the direct path |
| `$fp` and `$ra` | inherited renderer context and callback return continuation |

The caller sets the four vertex registers immediately before each `jalr`.
The callback intentionally uses volatile and saved registers outside o32
argument conventions, so `emit = false` is required.

## Culling and near path

At `0x8016FD34..0x8016FD4C`, the callback loads the four signed clip-flag
halfwords at record offset `+14` and ANDs them.  A nonzero common flag means
all vertices lie outside the same viewport plane; that edge returns without
changing the packet cursor.  The branch delay slot preloads vertex 0 depth.

For a surviving primitive, `0x8016FD60..0x8016FD94` loads all four unsigned
depths, computes their sum, and tests each against 125.  If any depth is below
125 it tail-transfers to `ClipAndSubmitNearTexturedQuad` at `0x80170B5C`.
That shared adapter performs software clipping and returns through this
callback's inherited `$ra`.

## Direct three-triangle path

When all depths are at least 125, `0x8016FD9C..0x8016FEF8` does the following:

1. Selects an ordering-table bucket from `(z0 + z1 + z2 + z3) >> 7`.
2. Averages the signed XYZ components of `$t8` and `$t9`, projects that edge
   midpoint with GTE `RTPS`, and retains its projected SXY.
3. Builds three adjacent records at `$s4 + {0,32,64}`.  Each record has the
   32-byte layout and command/UV/CLUT/TPAGE/XY fields of a textured triangle;
   the three packets reuse the original vertices and the new midpoint to
   cover the input quad.
4. Writes the packet-chain/ordering-table tags, then returns through `$ra`
   while advancing `$s4` by `0x60` in the delay slot.

The exact source typedefs and winding-oriented names of the three triangles
remain unrecovered.  Calling the result a triangle fan is supported by the
single shared midpoint, three equal packet strides, and packet chaining; it
does not imply an original SDK helper name.

## Symbol decision

Add `0x8016FD34` as `[[func]] SubmitTexturedQuadTriangleFan`, `status =
"known"`, and `emit = false`.  It is independently callback-addressed and
owns return behavior, so `[[block]]` would lose a real semantic boundary.
`known`, rather than `confirmed`, records that the boundary and behavior are
instruction-level facts while the original source name is not recovered.

## Evidence inspected

- `disc/SCES_028.45`: executable header, literal-word scan, and decoded
  direct-jump scan.
- `generated/SCES_028.45_full_140.c`: callback synthesis, four indirect call
  sites, `0x8016FD34..0x8016FEF8` control flow, GTE operation, packet stores,
  returns, and cursor advance.
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_dispatch.c`: generated range ownership, treated as
  AOT dispatch metadata rather than source-boundary authority.
- `docs/audits/semantic-8016fc28.md` and
  `docs/audits/semantic-80170b5c.md`: independently audited dispatcher and
  shared near-clipper contracts.
