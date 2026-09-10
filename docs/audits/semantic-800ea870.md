# Semantic audit: `0x800EA870`

## Identity and ABI

`0x800EA870` is the confirmed shared helper `ComputeObjectUpVectorQ12`:

```c
void ComputeObjectUpVectorQ12(int32_t out[3], const Object *object);
```

`a0` is a writable three-word output vector. `a1` is an object whose signed
angle halfwords are at `+0x11E`, `+0x120`, and `+0x122`. The routine establishes
a 48-byte frame, preserves `s0..s6` and `ra`, writes exactly `out[0..2]`, and
does not establish a scalar return value.

## Exact boundary and targets

The generated range table gives the owner as exactly
`[0x800EA870,0x800EA9B0)`, or `0x140` bytes. The prologue starts at
`0x800EA870`; the final stores are at `0x800EA970`, `0x800EA97C`, and
`0x800EA998`; `jr ra` is at `0x800EA9A8` with the frame restoration in its
delay slot at `0x800EA9AC`. The independent next owner begins at `0x800EA9B0`.

The dispatch continuations at `0x800EA8A8`, `0x800EA8B8`, `0x800EA8C8`,
`0x800EA8D8`, `0x800EA8E8`, and `0x800EA8F8` are returns from the six nested
trigonometry calls. A complete generated-source search finds no direct JAL or
jump to any of them, so none is a public alias or false split.

## Recovered behavior

Let the three signed angles be `ax`, `ay`, and `az`, and let `sx/cx`, `sy/cy`,
and `sz/cz` be their signed Q12 sine/cosine values. Angles use the game's
4096-units-per-turn convention and the six calls are to the already audited
`SinQ12FromSignedAngle` (`0x8015C318`) and `CosQ12FromSignedAngle`
(`0x8015C3E8`). Preserving the routine's staged arithmetic shifts, it computes:

```text
out.x = q12(cx * sz) - q12(q12(sx * sy) * cz)
out.y = q12(cy * cz)
out.z = q12(sx * sz) + q12(q12(cx * sy) * cz)
```

Here `q12(v)` is the routine's signed arithmetic shift right by 12 after a
32-bit signed multiply. At zero rotation the result is `(0, 4096, 0)`, proving
that the output is the object's rotated local `+Y` basis (its up vector), not a
general Euler-to-matrix builder. The helper reads no other object fields and
has no global or hardware side effects beyond the private sine/cosine table
reads performed by its callees.

## Exhaustive callers and overlay accounting

There are six generated entry calls and six unique guest call instructions:

| Call instruction | Containing generated owner | Argument evidence |
|---|---|---|
| `0x800E9194` | `0x800E913C` | output/work record and object-derived pointer |
| `0x800E9588` | `0x800E9510` | stack vector at `sp+0x18`, object pointer |
| `0x800E9D5C` | `0x800E9CB4` | stack vector at `sp+0x30`, object pointer |
| `0x800E9F6C` | `0x800E9F14` | output/work record and object-derived pointer |
| `0x800EA7AC` | `0x800EA760` | output/work record and object-derived pointer |
| `0x800F3190` | `0x800F2FF4` | stack vector at `sp+0x18`, object pointer |

Five calls reside in the base-address gameplay region and the sixth is in an
overlay owner at `0x800F2FF4`; none is a duplicated emission of the same guest
instruction. There are no interior callers. This census and the zero-angle
basis result support the conservative semantic name without asserting an
unproven source-level type name for the object.
