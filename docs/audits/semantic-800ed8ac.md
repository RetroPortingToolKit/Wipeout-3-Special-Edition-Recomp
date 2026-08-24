# Semantic audit: `0x800ED8AC`

## Result

`0x800ED8AC` is the confirmed signed-projection leaf
`ProjectPointDeltaOntoNormalQ12`. It receives two four-word vectors by value
through the o32 aggregate ABI plus a packed three-halfword normal in the
caller's stack argument area. It returns the signed component of the delta
from the first point to the second along that normal, divided by the normal's
Q12 squared magnitude.

A descriptive prototype is:

```c
int32_t ProjectPointDeltaOntoNormalQ12(
    Vec4i point_a, Vec4i point_b, PackedVec3s normal_q12);
```

The exact original typedef names are not proven. `Vec4i` here means four
32-bit words passed by value, and `PackedVec3s` means three signed halfwords.

## Exact boundary and leaf ownership

The preceding function returns and restores its 128-byte frame at
`0x800ED8A4..0x800ED8A8`. This function begins independently at
`0x800ED8AC`, never adjusts `sp`, never saves `ra`, and makes no calls. It
returns at `0x800ED980` with a NOP delay slot at `0x800ED984`. The next owner
opens a new frame at `0x800ED988`.

Its exact interval is therefore `[0x800ED8AC,0x800ED988)`, or `0xDC` bytes,
matching the generated range. Generated entries at `0x800ED940`,
`0x800ED944`, `0x800ED950`, `0x800ED954`, `0x800ED960`, `0x800ED968`,
`0x800ED96C`, `0x800ED97C`, and `0x800ED980` are internal compiler-generated
division branches and the common return, not callable functions. An aligned
scan finds no JAL to an interior address.

## Aggregate ABI and stack argument

Because the leaf is frameless, all stack offsets refer to storage allocated
by its caller. The layout at entry is:

| Location | Meaning | Used width |
|---|---|---:|
| `a0` | `point_a.x` | 32 bits |
| `a1` | `point_a.y` | 32 bits |
| `a2` | `point_a.z` | 32 bits |
| `a3` | `point_a.w/pad` | ignored |
| `sp+0x10` | `point_b.x` | 32 bits |
| `sp+0x14` | `point_b.y` | 32 bits |
| `sp+0x18` | `point_b.z` | 32 bits |
| `sp+0x1C` | `point_b.w/pad` | ignored |
| `sp+0x20` | `normal.x` | signed 16 bits |
| `sp+0x22` | `normal.y` | signed 16 bits |
| `sp+0x24` | `normal.z` | signed 16 bits |

All inspected callers materialize this exact layout: four 32-bit stores at
`sp+0x10..+0x1C`, followed by an unaligned copy of two normal halfwords and a
final signed-halfword store at `sp+0x24`. This proves that `sp+0x10` is a real
fifth-and-later ABI argument area, not this function's nonexistent local
frame.

The stores of `a0..a3` back to `sp+0..+0x0C` at
`0x800ED8E4..0x800ED8F0` populate the caller-reserved register-argument home
area. Nothing later reloads them, so these stores are ABI spill artifacts and
not an output vector or externally meaningful mutation.

## Exact arithmetic

Let:

```text
A = (a0, a1, a2)
B = (*(s32 *)(sp+0x10), *(s32 *)(sp+0x14), *(s32 *)(sp+0x18))
N = (*(s16 *)(sp+0x20), *(s16 *)(sp+0x22), *(s16 *)(sp+0x24))
```

The integer numerator is accumulated with ordinary low-32-bit MIPS
multiplication and addition:

```text
numerator = (A.x-B.x)*N.x + (A.y-B.y)*N.y + (A.z-B.z)*N.z
```

The normal components are copied to GTE IR1/IR2/IR3, and GTE command `SQR`
with `sf=1` produces the Q12-scaled component squares in MAC1/MAC2/MAC3. The
denominator is their 32-bit sum:

```text
denominator = (N.x*N.x >> 12)
            + (N.y*N.y >> 12)
            + (N.z*N.z >> 12)
```

Here the shifts describe the non-saturating authored input domain; GTE MAC
semantics govern extreme values. The returned signed quotient is:

```text
return -numerator / (denominator != 0 ? denominator : 1);
```

Equivalently:

```text
return dot(B-A, N) / Q12NormSquared(N);
```

MIPS signed division truncates toward zero. When `N` is a unit Q12 normal,
the denominator is approximately `0x1000`, so the result is the signed
coordinate-space distance along the normal. For a non-unit normal, the
division supplies the corresponding projection/intersection scalar. A zero
denominator is explicitly replaced with one, making the result zero when all
normal components are zero.

The apparent `break` instructions belong to the compiler's generic signed
division guards. The explicit zero substitution makes the divide-by-zero
branch unreachable for normal execution; the `INT_MIN / -1` guard is likewise
outside the nonnegative squared-magnitude domain.

## Call census

The retained executable contains 18 distinct direct guest call PCs:

```text
800E3D8C 800E4180 800F075C 800F0AEC 800F1498 800F6530
800F6684 800F6A68 800F7208 8012EC4C 8014E3AC 801541AC
80154200 80155CC8 80159150 80159778 8015AA04 8015AEF0
```

The generated corpus contains 22 caller instances because mutually exclusive
overlay translations duplicate four guest-address calls: `0x800E4180`
appears in three generated contexts, while `0x80159150` and `0x80159778`
each appear in two. Thus “22 callers” is correct for generated owner/context
instances, while the architectural call-site census has 18 unique PCs. The
audit records both counts to avoid treating overlay duplication as four new
guest addresses.

Callers use the quotient as a signed collision/geometry scalar: some branch
on it being positive, others add a fixed distance or carry it into subsequent
position calculations. None expects memory output from the leaf.

## Mutation and return limits

There are no callee calls, global reads, global writes, or pointed-to memory
accesses. Aside from the ABI home-area spills in caller-owned stack storage,
the function only mutates volatile registers and GTE IR/MAC data registers.
It does not preserve the prior GTE IR/MAC state. Its sole semantic result is
the signed 32-bit quotient in `v0`.

## Evidence

- `disc/SCES_028.45`: authoritative bytes and 18-PC direct/interior JAL scan;
- `generated/SCES_028.45_full.ranges`: exact `0xDC` range;
- `generated/SCES_028.45_full_17.c`: complete GTE, division, and return flow;
- generated caller translations: 22 context instances and their complete
  caller-owned stack argument layouts.
