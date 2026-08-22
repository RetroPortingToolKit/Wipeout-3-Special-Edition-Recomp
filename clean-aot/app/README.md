# Clean modern-PC application shell

## Production graphics path

`RuntimeGraphics` is the live application-owned bridge from the runtime GPU to
host presentation. In production scanout mode it drains the bounded native GPU
event queue into deterministic raster VRAM, derives scanout exclusively from
GP1 display timing, decodes RGB555 or packed 24-bit VRAM, and submits the result
to the host in the same update. The default remains exact 4:3; 16:9, 21:9,
32:9, and custom presentation are explicit configuration choices.

`PresentationResolveMode::EdgeAware` optionally resolves that authoritative
scanout to the planned 4K or ultrawide content dimensions before submission.
The resolve has an explicit pixel budget and falls back to the unchanged native
frame on allocation, dimension, or budget failure. The SDL/Vulkan host selects
linear final sampling only for an enhanced resolved frame; native mode retains
the original nearest sampler.

`DisplayPostprocessMode::ChromaSmooth24Bit` is an opt-in presentation-only
filter for packed 24-bit scanout. After the bounded high-resolution resolve it
smooths horizontal chroma variation only where both neighboring luma values
remain close, then restores the center pixel's luma. RGB555 scanout, native
resolve, disabled mode, and any allocation or pixel-budget failure retain the
unchanged frame. The fixed cost is three RGB reads, two luma comparisons, and
one RGB write per output pixel; HD UI replacement is composited afterward so
text and banners remain crisp.

`PreciseColorMode::TrueColorDisplay` is the opt-in true-color polygon seam.
Every precise fragment still performs the authoritative dithered RGB555 VRAM
write first. Safe opaque fragments additionally retain their pre-dither RGB888
color in a display-only plane, which scanout applies before bounded 4K resolve.
Semi-transparent framebuffer blends stay RGB555-only, and any later native
draw, fill, copy, or upload invalidates enhanced color only at destinations it
actually changes. Mask-rejected writes leave both authoritative and enhanced
color untouched. Packed 24-bit scanout bypasses this plane. Allocation, source
pixel-budget, missing-provenance, and disabled-mode paths preserve exact native
scanout. The display-plane merge performs one validity test and at most one RGB
write per native scanout pixel; the existing 4K resolve budget is unchanged.

Replacement assets remain separated from title policy. A title-owned key
policy may identify a texture or font while the generic runtime validates
manifest/source generations, confines paths to the asset root, verifies
SHA-256 and dimensions, decodes PNG to RGBA8 through Windows WIC, and passes
immutable pixels to an upload sink. Missing mappings retain native VRAM;
malformed declared assets fail closed.

This subtree is the smallest fail-closed consumer that joins the clean runtime,
generated-region dispatcher, and host contracts. It is not an emulator UI and
does not claim to boot WipEout 3.

Startup requires all of the following external setup products:

- a successful setup receipt and its measured BIOS;
- a successful finalize receipt and canonical finalized catalog;
- a successful exact-selection receipt plus the selected executable bytes;
- the measured generated native artifact beneath its declared artifact root;
- a compiled generated binding whose identity, load range, dispatch roots, and
  entries agree with the selected catalog variant.

Every file is re-measured before runtime or host initialization. Unknown,
tampered, non-finalized, path-traversing, or mismatched inputs fail closed. The
shell contains no compiler, guest-code decoder, interpreter, JIT, capture path,
game identity shortcut, or fallback dispatch. Its narrowly scoped WIC image
decoder accepts only hash- and dimension-pinned replacement PNGs.

`Application` is platform-neutral and drives one authenticated generated
region, deterministic runtime advancement, frame submission, exact 441-frame
audio batches, controller state, and bounded host service per update.
`SdlPlatform`, enabled with `-DCLEAN_AOT_APP_ENABLE_SDL=ON`, is the minimal
native adapter: a resizable high-DPI window, SDL audio stream, bounded event
pump, and first-gamepad sampling. It deliberately has no emulator menus.

The test target generates a tiny synthetic catalog, receipts, executable,
artifact, BIOS, and native region. Passing it proves the complete application
boundary without fabricating BIOS or game boot.
