# Semantic audit: guest `0x801700C4`

## Result

`0x801700C4` is the true entry of a callable renderer leaf, not an internal
label in `0x8016FF28` and not merely the prefix of `0x801700F0`.  A useful
source-level name is:

```text
SubmitTexturedQuad
```

The routine trivial-rejects a textured quad from the common clip flags of four
already projected vertices.  A visible quad whose four depths are at least 125
is emitted as one 40-byte `POLY_FT4` and linked into the ordering table.  A quad
crossing the near threshold tail-transfers to the shared clipper at
`0x80170B5C`.

## Boundary evidence

The generated split at `0x801700C4` agrees with the machine-code boundary:

- the preceding routine, `0x8016FF28`, ends with `jr $ra` at `0x801700BC` and
  uses `0x801700C0` only as its return delay slot;
- `0x801700C4` has its own return at `0x801700E8` for trivial rejection;
- its accepted path branches into the continuation at `0x801700F0`, which
  either returns through the same inherited `$ra` or transfers to
  `0x80170B5C`;
- a scan found no `j`/`jal` instruction targeting it.  Raw byte occurrences of
  its address are unaligned and are not strong enough to claim a recovered
  pointer-table caller.  Thus the static corpus proves a callable-shaped entry
  and its contract, but not a particular call site (an indirect call or an
  unloaded/overlaid caller remains possible);
- by contrast, no address-bearing data or direct call establishes
  `0x801700F0` as independently callable.  It remains a `[[block]]`.

The lack of an o32 prologue is expected: this is a small assembly-style leaf
using a private register contract.  It does not merge with the physically
preceding function across an unconditional return.  The adjacent sibling at
`0x8016FEFC` has the same four-outcode wrapper shape and transfers into its own
packet path at `0x8016FF28`, further supporting `0x801700C4` as a sibling entry
rather than a continuation of `0x8016FF28`.

## Effective signature

The most accurate current signature is a private register-ABI operation rather
than a normal C prototype:

```text
void SubmitTexturedQuad(
    projected_vertex *v0 /* $t6 */,
    projected_vertex *v1 /* $t7 */,
    projected_vertex *v2 /* $t8 */,
    projected_vertex *v3 /* $t9 */,
    texture_template *texture /* $v0 */,
    face_material *face /* $t4 */,
    ordering_table *ot /* $s2 */,
    POLY_FT4 **packet_cursor /* value held in $s3 */);
```

These type names are descriptive, not recovered canonical typedef names.  The
observable fields are stronger evidence than the names:

| Register | Input/use |
|---|---|
| `$t6..$t9` | four projected-vertex records: packed XY at `+8`, unsigned depth at `+12`, signed clip flags at `+14`, and next/value link at `+8` after emission |
| `$v0` | three packed texture/template words used for UV, CLUT, and TPAGE fields |
| `$t4` | face/material record; unaligned word at `+18` supplies packet color/command bytes |
| `$s2` | ordering-table base |
| `$s3` | current packet address, advanced by `0x28` after emission |
| `$ra` | caller continuation, preserved across the fast and reject paths and consumed by the slow tail path |

There is no scalar return value.  The meaningful results are the GPU packet,
ordering-table link, advanced packet cursor, and advanced vertex cursors.

## Fast and slow paths

At `0x801700C4`, the routine ANDs all four `vertex + 14` clip/outcode fields.
A nonzero common mask means every vertex lies outside the same plane, so it
returns immediately.  Otherwise the branch delay slot loads vertex 0 depth and
control reaches `0x801700F0`.

The continuation loads the other three depths, calculates the ordering bucket
from `(z0 + z1 + z2 + z3) >> 7`, and tests every depth against 125.  If none is
near, it builds and OT-links a standard 40-byte `POLY_FT4`: XY words occupy
packet offsets `+8/+16/+24/+32`, and the remaining halfword stores match the
UV/CLUT/TPAGE fields.  It then advances `$s3` by 40 and returns.

If any depth is below 125, the routine tail-transfers to `0x80170B5C`.  That
path saves the live private-register context before clipping and packet
generation.  It is a shared slow continuation, not evidence that the caller
entered at `0x801700F0`.

## Symbol decisions

- `0x801700C4`: `[[func]] SubmitTexturedQuad`, `status = "known"`, but
  `emit = false` because a normal host-callable ABI wrapper has not been proven.
- `0x801700F0`: retain `[[block]] EmitTexturedQuadFastPath`.
- `0x80170B5C`: shared slow-path target; this audit does not assert a separate
  public ABI for it.

## Authoritative evidence inspected

- `generated/SCES_028.45_full_140.c`, instructions and control flow from
  `0x8016FF28..0x801701B0`, plus the slow target at `0x80170B5C`.
- `generated/SCES_028.45_full.ranges`, entries around `0x8016FF28`,
  `0x801700C4`, and `0x801700F0`.
- `generated/SCES_028.45_dispatch.c`, generated entry/continuation coverage.
- `disc/SCES_028.45`, scanned for direct jump/call encodings and address-bearing
  occurrences of `0x801700C4` and `0x801700F0`.
