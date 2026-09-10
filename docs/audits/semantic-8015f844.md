# Semantic audit: `0x8015F844`

## Result

`0x8015F844` is the confirmed PsyQ-compatible graphics SDK routine
`SetDefDrawEnv`. It initializes the public fields of a `DRAWENV` structure
from a rectangle, chooses the draw-to-display-area flag from the current video
mode and requested height, and returns the original environment pointer.

Its ABI is the standard five-argument form:

```c
DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h);
```

## Boundary and call graph

The preceding owner returns at `0x8015F834` with its final store in the delay
slot at `0x8015F838`; `0x8015F83C..0x8015F840` are NOP alignment. This owner
opens a 40-byte frame at `0x8015F844`, saves `ra` and `s0..s4`, and returns at
`0x8015F8F0` with its frame restore at `0x8015F8F4`. Its exact interval is
therefore `[0x8015F844,0x8015F8F8)`, or `0xB4` bytes, matching the generated
range. Three NOPs at `0x8015F8F8..0x8015F900` separate it from `VSync` at
`0x8015F904`.

Generated entries at `0x8015F878`, `0x8015F8B0`, `0x8015F8B8`, and
`0x8015F8BC` are post-call/branch continuations retaining the owner's frame.
An exhaustive aligned scan finds no call to an interior address.

There are exactly 20 direct callers, all within the graphics-environment setup
owner beginning near `0x800EDA90`:

```text
800EDAB8 800EDAD4 800EDAF0 800EDB0C 800EDB30
800EDB48 800EDB60 800EDB78 800EDB98 800EDBB0
800EDBC8 800EDBE0 800EDC00 800EDC18 800EDC30
800EDC48 800EDC60 800EDC78 800EDC90 800EDCA8
```

Destinations are separated by `0x5C` bytes within several environment arrays.
Callers supply `(x,y)` values of 0 or 256, widths of 256 or 512, and heights
of 128 or a value held in `s2`, consistent with framebuffer subdivisions and
the recovered clip rectangle—not packet coordinates or a display-mode setter.

The sole callee is the four-instruction getter at `0x8015F7D8`, which returns
the video-mode word stored through `0x801790A8`. No BIOS call or direct GPU
register write occurs in `SetDefDrawEnv` itself.

## Fifth argument and truncation

After subtracting 40 from `sp`, the function loads `h` from `sp+0x38`. This
is the caller's original `sp+0x10`, the standard o32 slot for argument five.
It is therefore a real scalar stack argument rather than a local variable.

`x`, `y`, `w`, and `h` are stored with `sh`. Values outside 16 bits truncate
to their low halfwords. The `dfe` height comparison, however, uses the full
signed 32-bit incoming `h` preserved in `s2`, not the truncated stored field.

## Exact `DRAWENV` layout and writes

The routine performs the following writes relative to `env`:

| Offset | Width | Field | Value |
|---:|---:|---|---|
| `+0x00` | 16 | `clip.x` | `(int16_t)x` |
| `+0x02` | 16 | `clip.y` | `(int16_t)y` |
| `+0x04` | 16 | `clip.w` | `(int16_t)w` |
| `+0x06` | 16 | `clip.h` | `(int16_t)h` |
| `+0x08` | 16 | `ofs[0]` | `(int16_t)x` |
| `+0x0A` | 16 | `ofs[1]` | `(int16_t)y` |
| `+0x0C` | 16 | `tw.x` | 0 |
| `+0x0E` | 16 | `tw.y` | 0 |
| `+0x10` | 16 | `tw.w` | 0 |
| `+0x12` | 16 | `tw.h` | 0 |
| `+0x14` | 16 | `tpage` | 10 |
| `+0x16` | 8 | `dtd` | 1 |
| `+0x17` | 8 | `dfe` | mode/height predicate below |
| `+0x18` | 8 | `isbg` | 0 |
| `+0x19` | 8 | `r0` | 0 |
| `+0x1A` | 8 | `g0` | 0 |
| `+0x1B` | 8 | `b0` | 0 |

No bytes at or beyond `env+0x1C` are touched. In particular the embedded SDK
draw-environment command workspace that makes the containing structure
`0x5C` bytes remains uninitialized by this routine. This matches the role of
`SetDefDrawEnv`: establish defaults, leaving command materialization to the
later draw-environment submission path.

## Video-mode-dependent `dfe`

After the field initialization, the function tests the scalar result from
`0x8015F7D8`:

```c
if (GetVideoMode() != 0)
    env->dfe = (h < 289); // 0x121, PAL-height boundary
else
    env->dfe = (h < 257); // 0x101, NTSC-height boundary
```

The comparisons are signed and strict. Thus heights 288/256 set `dfe`, while
289/257 clear it in the corresponding mode. This is the canonical distinction
between drawing inside the visible-height limit and drawing to a taller
interlaced/display-area surface. The routine does not modify the global video
mode.

## Return and side effects

`v0` is set to `env` before the final common field stores and remains unchanged
through the epilogue. The exact return is therefore the same pointer supplied
in `a0`; it is not a status code. The only external effects are the listed
environment writes. There are no global writes, GPU register writes, heap
operations, callbacks, or error paths.

## Behavioral equivalent

```c
DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h) {
    int mode = GetVideoMode();
    env->clip = (RECT){(short)x, (short)y, (short)w, (short)h};
    env->tw = (RECT){0, 0, 0, 0};
    env->dtd = 1;
    env->dfe = mode ? h < 289 : h < 257;
    env->ofs[0] = (short)x;
    env->ofs[1] = (short)y;
    env->tpage = 10;
    env->isbg = env->r0 = env->g0 = env->b0 = 0;
    return env;
}
```

The write order in the executable differs from this compact presentation but
the final field values are exact.

## Evidence

- `disc/SCES_028.45`: exact disassembly and complete direct/interior-JAL scan;
- `generated/SCES_028.45_full.ranges`: exact `0xB4` owner range;
- `generated/SCES_028.45_full_52.c`: complete frame, field stores, branch,
  getter call, and return;
- all 20 caller sites in the graphics-environment initializer.
