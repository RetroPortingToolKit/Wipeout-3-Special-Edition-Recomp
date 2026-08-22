# Clean AOT PS1 reference raster and presentation component

This directory is a standalone C++20 reference implementation for the clean
GPU event contract. It owns an exact 1024x512 array of 16-bit VRAM words, or
can bind a caller-owned exact-size VRAM span for synchronous execution, and
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

`vram_generation()` is the monotonic source identity used by replacement-
texture caches. Reset and each accepted VRAM-writing event advance it;
downloads, state markers, and rejected or ignored draws do not. The generation
is deliberately global and conservative: a native backend may add finer page
tracking, but may never reuse a replacement after this identity changes.

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

The precise-polygon path also offers an explicit
`BilinearTransparencySafe` filter. It performs a fixed four-sample bilinear
blend only when every texel in the footprint is visible and carries the same
PS1 semi-transparency bit. Transparent or mixed-STP footprints use native
nearest sampling, preventing color bleed across cutout edges and preserving the
hardware blend class. `NearestNative` remains the default and is byte-identical
to the unenhanced path. Counters expose fragment, fetch, and fallback costs for
deterministic performance receipts.

Near-plane handling is likewise explicit. `PreciseNearPlanePolicy::NativeFallback`
is the default: mixed-sign, zero, or sub-near W rejects the enhanced primitive
before VRAM changes so the unchanged GP0 event can run natively. A producer
that has proved positive-W clip-space convention may request `ClipPositiveW`.
The rasterizer then clips each provenance-complete source triangle at a finite
W plane, interpolates homogeneous screen position and vertex UV/color along
the clip edges, and emits an ordered three- or six-vertex fan. Quad source
triangles remain in GP0 order, and the normal half-open edge rule gives their
generated diagonal one owner. Counters distinguish clipped source primitives
from generated triangles.

## Live GP0 compatibility matrix

The application drains every decoded primitive through one authoritative VRAM
path. Enhanced geometry is attempted only for polygons with complete current
provenance; rejection immediately consumes the unchanged event natively.

| GP0 family | Native coverage | Enhanced coverage |
| --- | --- | --- |
| Flat/Gouraud triangles | draw offset, clip, dither, blend and mask | PGXP position/W, perspective color/UV, native or expanded culling |
| Flat/Gouraud quads | two triangles with one-owner shared edge | same half-open edge ownership; no duplicate diagonal blend |
| Textured polygons | 4/8/15-bit, CLUT, window, raw/modulated, STP | same texture state, replacement handle and transparency-safe filtering |
| Flat/Gouraud lines and polylines | joins, offsets, dither, blend and mask | authoritative native fallback; never silently discarded |
| Tiles and textured sprites | variable/1/8/16 sizes, flips, window, blend and mask | native raster plus safe generation-checked HD UI overlay |
| Fill/copy/upload/download and environment | wrapped VRAM and mask semantics | authoritative state/source generation for later enhanced draws |

The deterministic live matrix test frames these packet families together and
requires all polygon variants to reach precise raster with zero fallback. A
separate identity-projection test requires a semi-transparent precise quad to
match the complete native VRAM digest, protecting shared-edge blend and mask
semantics.

`PreciseColorMode::TrueColorDisplay` does not turn VRAM into a true-color
framebuffer. Precise fragments always quantize, dither, blend, and set/check
mask bits in authoritative RGB555 storage. An opaque successful write may also
retain its pre-dither RGB888 result for display scanout. Any later write that
actually reaches that VRAM pixel invalidates the retained color; rejected mask
writes do not. Semi-transparent fragments never enter the display plane because
their result depends on RGB555 framebuffer feedback. This gives scaled
true-color Gouraud gradients and removes enlarged 4x4 dither noise without
changing texture reads, copies, downloads, blending, masks, or guest-visible
VRAM. `NativeRgb555` remains byte-identical and is the default.

`ScanoutMapper::upscale_edge_aware_rgb888()` is the high-resolution
presentation resolve. It applies the Scale2x edge rules directly at an
arbitrary 2x-or-greater target size, selecting only colors already present in
the authoritative PS1 scanout. It therefore cannot alter VRAM blending, mask
bits, framebuffer feedback, or 15/24-bit display decoding. Work is linear and
bounded per output pixel: five source-pixel reads, at most three RGB
comparisons, and one RGB write. Invalid dimensions, underscaling, oversized
surfaces, and wrong buffer extents are rejected before output is touched.

`ScanoutMapper::chroma_smooth_rgb888()` is a separate display-domain seam for
decoded packed 24-bit content. It uses a horizontal 1:2:1 chroma kernel only
when both neighbors are within a bounded luma threshold, reconstructs RGB at
the center luma, and never reads or writes VRAM. Work is fixed at three RGB
reads, two luma comparisons, and one RGB write per pixel. Source/destination
aliasing, malformed extents, and oversized surfaces are rejected before any
destination byte changes; callers retain the unprocessed frame as fallback.
