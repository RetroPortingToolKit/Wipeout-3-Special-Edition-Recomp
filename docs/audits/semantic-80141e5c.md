# Semantic audit: `0x80141E5C`

## Result

`0x80141E5C` is the callable texture-resource adapter
`CreateTextureDescriptorFromImageData`.

The generated base range is exactly `R 80141E5C 24`, so the function occupies
36 bytes (`0x80141E5C..0x80141E7F`). It has a conventional 24-byte frame and
returns through the `jr ra` at `0x80141E78`, whose delay slot restores `sp` at
`0x80141E7C`. The next independent callable begins at `0x80141E80`.

## Proven behavior

The body performs only one argument conversion and one call:

- `0x80141E60` shifts `a1` left by 16 and `0x80141E6C` arithmetic-shifts it
  back, canonicalizing the mode argument to signed 16 bits.
- It does not modify `a0`, which remains the caller-provided image-data
  pointer.
- `0x80141E68` calls the full builder at `0x8014195C`.
- No instruction changes `v0` after that call, so the builder's returned
  descriptor is returned unchanged.

The delegated builder allocates 38 bytes, fills a halfword-oriented texture
descriptor, derives texture-page/CLUT fields, uploads the image rectangles,
and returns the allocated descriptor. This adapter therefore owns neither the
allocation nor upload algorithm; its distinct semantic role is the stable
two-argument entry with signed-mode normalization.

## Callers

There are four direct `jal 0x80141E5C` sites in the generated image:

- `0x8011093C`, inside callable `0x80110894`, passes mode `0`.
- `0x80126BD8`, inside callable `0x80126BB8`, passes image data at
  `0x80047980` and mode `1`.
- `0x80157840`, inside callable `0x801577B0`, passes image data at
  `0x80047140` and mode `1`.
- `0x80157858`, in the same callable, passes image data at `0x80047DC0` and
  mode `1`.

The latter three immediately publish `v0` into persistent globals or owner
state, consistent with a returned resource descriptor. No generated call
targets an interior address, and no address-taken reference was found.

## Rejected interpretations

- It is not the texture parser/uploader itself; that implementation is the
  1,040-byte callee at `0x8014195C`.
- It is not an internal continuation of that callee: it has its own frame,
  four direct callers, and a complete return sequence.
- The second argument is not an arbitrary 32-bit flag word because the wrapper
  deliberately sign-extends its low halfword before delegation.
