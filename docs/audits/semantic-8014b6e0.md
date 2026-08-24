# Semantic audit: `0x8014B6E0` / `SetVector`

## Result

`0x8014B6E0` is the PsyQ-compatible vector initializer `SetVector`:

```c
void SetVector(VECTOR *dst, int32_t x, int32_t y, int32_t z);
```

It writes the three signed 32-bit components and deliberately leaves the
fourth/padding word of the 16-byte `VECTOR` structure unchanged.

## Exact boundary, ABI, and return

The complete owner is `[0x8014B6E0,0x8014B6FC)`, exactly `0x1C` bytes and
matching the generated range table:

```text
8014B6E0  sw  a1,4(sp)
8014B6E4  sw  a2,8(sp)
8014B6E8  sw  a3,0xC(sp)
8014B6EC  sw  a1,0(a0)
8014B6F0  sw  a2,4(a0)
8014B6F4  jr  ra
8014B6F8  sw  a3,8(a0)       # return delay slot
```

The preceding leaf returns at `0x8014B6D8` with its delay slot at
`0x8014B6DC`; the next independent owner begins at `0x8014B6FC`. Thus no
adjacent instruction belongs to this function.

The first three stores populate the standard o32 argument-home area and do
not change the formula. `a0` is the destination pointer; `a1`, `a2`, and `a3`
are the three component values. The function does not assign `v0`, and every
caller ignores its incidental incoming value, so there is no semantic return.

## State formula

The only destination-object effects are:

```c
dst->vx = x;  // +0x00
dst->vy = y;  // +0x04
dst->vz = z;  // +0x08
               // +0x0C pad remains unchanged
```

All values are copied bit-for-bit as full words. Interpreting them as signed
32-bit coordinates follows the PsyQ `VECTOR` layout and the callers' use of
signed geometry values; there is no narrowing, saturation, rounding, scaling,
or arithmetic. The routine is frameless, calls no subroutine, and changes no
global state.

Several callers also place a fifth word at outgoing stack offset `+0x10` while
copying a larger source record. This routine never reads that word, which
independently confirms that it initializes only the three meaningful VECTOR
components and does not copy the pad.

## Direct callers and interior targets

An exhaustive aligned instruction-word scan of the retained executable finds
exactly 16 direct calls:

```text
800EC2C0 800EC2F4 800EC328 800EC35C 800EC390 800EC3AC
800EC3D0 800EC3EC 80147464 80147480 8014749C 801474B8
8014ABD8 8014ABF4 8014AC10 8014AC2C
```

Every `j`/`jal` target in `[0x8014B6E0,0x8014B6FC)` is one of those entry
calls. There are no direct interior targets, so the argument-home stores are
not a generated prefix for a separately callable tail.

Representative caller windows pass a destination in `a0`, load three signed
coordinate words into `a1` through `a3`, call this owner, and then continue
without consuming `v0`. Repeated calls at `0x800EC2C0..0x800EC3EC`,
`0x80147464..0x801474B8`, and `0x8014ABD8..0x8014AC2C` construct arrays or
groups of vectors.

## Identity proof

The exact three full-word writes at VECTOR component offsets, preservation of
the pad word, void caller contract, and repeated geometry-call pattern match
the PsyQ `SetVector` helper rather than a generic four-word record copy.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions and exhaustive aligned caller/interior-target scans;
- `generated/SCES_028.45_full.ranges` for the exact generated owner boundary;
- representative caller windows for the o32 ABI, ignored return, component
  sources, and untouched fifth outgoing argument.
