# Clean AOT PS1 reference raster and presentation component

This directory is a standalone C++20 reference implementation for the clean
GPU event contract. It owns an exact 1024x512 array of 16-bit VRAM words and
consumes `const clean_aot::gpu::Event&` values without retaining or mutating
them. It has no window-system, SDL, Vulkan, or D3D dependency.

The public API is
`include/clean_aot/raster/raster.hpp`. `ReferenceRasterizer` is the bounded
reference backend. `NativeBackendV1` is a non-owning function table: a native
backend receives the same immutable event pointer and caller-owned work budget,
and can optionally expose a VRAM readback, digest, and presentation plan. The
table has no virtual inheritance, hidden ownership, or graphics-library types.
It is a same-process C++ seam, not a C ABI: producer and consumer must use a
compatible compiler, standard library, build mode, and `gpu::Event` definition.
The table and every returned VRAM span become invalid when their originating
`ReferenceRasterizer` is destroyed; the span's contents can change after any
non-const rasterizer call.

## Raster contract

Primitive coordinates receive the immutable draw offset, are clipped to the
inclusive draw area and physical VRAM, and never wrap. Triangle samples use
integer raster coordinates and a top-left half-open edge rule; lower and right
edges are excluded. Color and UV planes use signed 12-bit fractional gradients,
a half-unit starting bias, and truncating division. Quads split into triangles
`(0,1,2)` and `(1,2,3)`, so their shared edge is owned once. Lines use the PS1's
bounded 32-bit-fractional DDA with inclusive endpoints and 12-bit-fractional
Gouraud attributes. Every polyline segment emits both endpoints, so a shared
semi-transparent join is blended once by each adjacent segment. Rectangles use
their effective event width and height, with the GP0 rectangle flip bits
reversing the texture-coordinate walk. Polygon/line edges over 1023 pixels in X
or 511 pixels in Y are discarded before drawing.

The ordered 4x4 dither matrix is applied before 5-bit packing for all lines and
for Gouraud or modulated polygons. Flat untextured polygons, raw-texture
polygons, and every rectangle command are not dithered. Texture pages use the
producer's 64-word horizontal and 256-line vertical units. Indexed-4 samples
select one nibble from a page word, indexed-8 samples select one byte, and
direct-15 samples select one VRAM word; the reserved depth aliases direct-15.
Indexed samples read their CLUT from VRAM. The texture window first wraps U/V to
eight bits, then applies the mask/offset replacement.

Only the complete texture word `0x0000` is transparent; `0x8000` is visible
black with its mask/semi-transparency bit set. On a semi-transparent textured
command, blending applies only to texels whose bit 15 is set. When mask forcing
is disabled, a textured output inherits the sampled texel's bit 15. Modulated
textures use the native five-bit channel calculation
`(texel5 * vertex_color8) >> 4`; raw textures bypass modulation.

Primitive semi-transparency uses the four immutable draw-mode operations:

- `(background >> 1) + (foreground >> 1)`;
- `clamp(background + foreground)`;
- `clamp(background - foreground)`;
- `clamp(background + (foreground >> 2))`.

The mask check is performed against the destination before a primitive write,
and the mask-set bit is applied to the written word. Fill ignores mask state.
VRAM copies snapshot one source row, write that row, and then advance top to
bottom. This gives source-preserving horizontal overlap and the hardware's
observable propagation on downward vertical overlap. Transfer coordinates wrap
independently at 1024x512. Uploads consume low/high halfwords in row-major order,
honor odd-pixel padding, and apply mask state per halfword. Downloads are a
zero-copy view of the immutable event payload and do not alter VRAM.

Transfer events are already-decoded contract values. Raw GP0 fill alignment,
size masking, and zero-size normalization belong to the GPU event producer and
must occur before this component receives an event. `read_vram()` wraps public
coordinates to the physical VRAM dimensions, matching transfer addressing.

Unsupported texture depths, malformed event ownership, invalid transfer
payload sizes, excessive line vertices, and work over the caller's budget fail
closed before the corresponding event changes VRAM. A batch may contain at
most the caller's event count and work budget. The default budget is finite
and can cover a full-screen reference primitive or a two-triangle full-screen
quad.

The digest is FNV-1a over VRAM halfwords in little-endian byte order. This is
an intentionally simple deterministic golden-test contract, not a claim about
GPU bus timing.

## Deliberate boundary and integration requirements

This is a functional VRAM reference, not a complete CRTC or timed GPU. It does
not model FIFO/bus timing, texture-cache staleness, field timing, or a 2 MB
VRAM extension. Cache-flush events are accepted as
ordering markers because texture sampling currently reads VRAM directly.

`ScanoutMapper` is the typed production boundary for the current GP1 display
state. It retains the raw horizontal/vertical display ranges, resolves the
nominal 256/320/368/512/640-pixel mode, exposes a 480-line interlaced frame as
an explicit even or odd 240-line field, and decodes wrapped VRAM into caller-
owned RGB888 storage. Both native RGB555 expansion and packed 24-bit byte
layout are deterministic and bounded. A disabled display decodes to black.
This is spatial scanout extraction only; field cadence, deinterlacing, scaling,
HDR conversion, and host presentation remain native-backend policy.

The current GPU event snapshot does not carry the active GP1 display rectangle.
Therefore GP0(E1).10 "draw to display area" gating cannot be enforced here: a
caller or native backend that requires exact behavior must combine draw-mode
bit 10 with current CRTC display state before writing. Presentation mapping is
likewise geometry only; it does not add scanout timing or display-area policy.
These are explicit upstream/native-backend integration requirements, not
capabilities of `ReferenceRasterizer`.

See `PROVENANCE.md` for the source-isolation record and pinned behavioral
evidence.

## Presentation contract

`PresentationMapper::map()` is pure geometry. `Identity4x3` exposes the caller's
source rectangle as the logical 4:3 surface and fits that surface into the
requested output with centered integer letterbox/pillarbox margins. It does
not crop, stretch, or infer a title-specific policy. `Generic16x9`,
`Generic21x9`, `Generic32x9`, and `Custom` are explicit composition seams: they
only select a destination aspect ratio and leave the source rectangle and
composition decisions to the caller/native backend.

## Build and verification

Build outside this source directory so generated files do not enter the
cleanroom tree:

```text
cmake -S clean-aot/raster -B build/clean-aot-raster -DCMAKE_BUILD_TYPE=Release
cmake --build build/clean-aot-raster --parallel
ctest --test-dir build/clean-aot-raster --output-on-failure
```

Enable sanitizers on GCC or Clang with
`-DCLEAN_AOT_RASTER_ENABLE_SANITIZERS=ON`. The tests cover transfer handoff,
masking, hardware-captured overlap ordering, all texture depths, texture
windows, modulation/raw paths, semi-transparency, dither, draw clipping and
offset, fixed-point line behavior, full hardware-captured Gouraud and UV/color
interpolation scenes, presentation aspect seams, backend dispatch, work bounds,
and a project-scene VRAM golden digest.
