# Semantic audit: guest `0x80170B5C`

## Result

`0x80170B5C` is a real shared function boundary, but it is entered only as a
renderer-private tail call in the retained image.  A defensible source-level
name is:

```text
ClipAndSubmitNearTexturedQuad
```

The function is an adapter around `SceneryFrustumClip` (`0x80142634`).  It
copies the four projected vertices and the live textured-face state into the
global clipping workspace at `0x801E25D0`, requests a four-vertex clip, calls
the clipper, restores the complete inherited register context, and returns to
the original renderer caller.  It does not itself contain clipping passes,
midpoint subdivision, or direct GPU packet construction.

## Boundary and entry status

The machine-code boundary is unusually strong despite the private ABI:

- `0x80170B5C` begins with an 80-byte stack allocation and saves `$at`, `$gp`,
  `$fp`, `$t0..$t5`, `$k1`, `$v0`, `$s0..$s7`, and `$ra`;
- it makes one ordinary `jal 0x80142634` and resumes at `0x80170C64`;
- `0x80170C64..0x80170CB0` restores every saved register;
- `0x80170CB4` executes `jr $ra`, with stack restoration in its delay slot;
- the next routine begins independently at `0x80170CBC`.

Three retained branches target the entry, from the near-depth tests at
`0x8016FD94`, `0x8016FF5C`, and `0x80170124`.  They are tail transfers rather
than `jal` calls: the inherited `$ra` is deliberately retained so the shared
function returns directly to the renderer's caller.  There is no fallthrough
into the entry and no evidence that `0x80170B5C` is a public o32 entry.

This makes `[[func]]` more accurate than `[[block]]`: it owns a complete frame,
contains a call, has multiple independent incoming edges, and owns its return.
It remains `emit = false`, because exposing it through a normal four-argument
host ABI would be incorrect.

## Private register contract

All three tail callers preserve the same live renderer state.  The effective
contract is best recorded as register dataflow rather than a C prototype:

| Register/state | Meaning used by the adapter |
|---|---|
| `$t6`, `$t7`, `$t8`, `$t9` | four projected-vertex record pointers |
| `$v0` | texture/template record: packed words at `+0`, `+4`, and `+8` |
| `$t4` | face/material record; word at `+16` supplies packet color/command data |
| `$fp` | inherited renderer frame; words at `+48` and `+12` restore `$gp` and the caller's `$fp` before delegation |
| `$ra` | original renderer return continuation, preserved across the clipper call |

The wrapper intentionally saves substantially more state than an o32 callee.
That is necessary because the tail callers keep packet, ordering-table, face,
and projected-vertex cursors in volatile and saved registers.  The only
meaningful outputs are therefore the global renderer/clipper side effects.

## Workspace record written at `0x801E25D0`

The entry loads `$s7 = 0x801E25D0` and writes the following demonstrated
fields before calling the clipper:

| Workspace offset | Source / role |
|---|---|
| `+0x00` | mode byte = 1 |
| `+0x4C`, `+0x4E` | high and low halfwords of texture word `+0` |
| `+0x52` | primitive/type byte = 5 |
| `+0x1C` | face/material word `+16` |
| `+0x2C`, `+0x30` | texture/template words `+4`, `+8` |
| `+0x34..+0x3A` | signed X values from the four projected vertices |
| `+0x3C..+0x42` | signed Y values from the four projected vertices |
| `+0x44..+0x4A` | signed Z values from the four projected vertices |
| `+0x50` | vertex count = 4 |

Each source projected vertex uses its signed halfwords at offsets `+0`, `+2`,
and `+4`.  Thus this path passes camera/projected XYZ geometry to the software
clipper, not merely the packed screen XY and depth fields used by the fast
`POLY_FT4` emitter.

`SceneryFrustumClip` reads the mode at workspace `+0`; with mode 1 it uses the
full viewport clip coefficients rather than the optional half-scale values.
The callee performs the actual clipping against the camera side planes and
owns any resulting packet emission through the shared workspace.  No evidence
in this wrapper supports describing its behavior as recursive quad
subdivision.

## Relationship to the fast renderer paths

The three incoming edges are reached after the corresponding renderer has
already rejected a primitive with a common clip outcode and found at least one
vertex depth below 125.  The sibling path audited at `0x801700C4` emits one
40-byte `POLY_FT4` directly when all four depths are at least 125; its slow edge
at `0x80170124` instead transfers here.  The two earlier sibling render paths
at `0x8016FD60` and `0x8016FF28` use the same shared adapter.

Consequently the precise claim is "near textured-quad clipping and submission
adapter."  Packet fan-out, interpolation, and ordering-table effects belong to
`SceneryFrustumClip`, not to this 352-byte wrapper itself.

## Symbol decision

Add:

```toml
[[func]]
pc = 0x80170B5C
name = "ClipAndSubmitNearTexturedQuad"
emit = false
status = "known"
```

The `known` status reflects exact boundary, inputs, marshaling, and delegation.
It is not `confirmed` because original source nomenclature and canonical
structure types remain unrecovered.

## Authoritative evidence inspected

- `generated/SCES_028.45_full_140.c`, instructions and control flow for the
  three incoming branches and `0x80170B5C..0x80170CB8`.
- `generated/SCES_028.45_full.ranges`, the `F 80170B5C` / 352-byte range.
- `generated/SCES_028.45_dispatch.c`, entry and `0x80170C64` continuation.
- `docs/audits/semantic-801700c4.md`, the independently audited fast/near
  decision and private register contract.
- `docs/audits/semantic-clip-globals.md`, the clipper mode and side-plane
  coefficient behavior.
