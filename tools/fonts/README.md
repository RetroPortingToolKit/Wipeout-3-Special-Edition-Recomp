# Font atlas replacement

WipEout 3 draws all its text from bitmap font atlases in VRAM. At high internal
resolution those are the most visible remaining artefact: everything else is
geometry, but the type is a 1999 bitmap magnified.

These tools build high-resolution replacements from vector typefaces, laid out
on the *exact* cells the game samples, so no string shifts.

## The atlases

Identified by dumping textures (`[video] hd_texture_dump = true`) and reading
the result:

| texture   | size   | typeface                | contents                          |
|-----------|--------|-------------------------|-----------------------------------|
| `75f5e38b`| 256x56 | F500 Ang-ular (display) | `a-u` / `v-z0-9:./+-%` / arrows   |
| `4cb2ec4e`| 256x56 | F500 Ang-ular (smaller) | same set, tighter                 |
| `d20dee94`| 256x36 | Arial/Helvetica-ish     | lower, digits, upper, symbols     |
| `861ad9f1`| 256x32 | Arial/Helvetica-ish     | same set, smaller                 |

Each appears under several palette hashes (the same glyphs drawn in different
colours). A replacement is needed per `<texhash>-<palhash>` pair.

## Getting the cell layout

This is the part that has to be right, and it cannot be eyeballed. The atlases
are *proportional* — every glyph is a different width — and they carry no grid:
rows are packed with no separating scanline, and in the Arial-ish atlases
adjacent glyphs touch, so scanning the bitmap for gaps merges them (one ink run
in `d20dee94` spans 84 px and covers many characters).

The game states the layout itself. Every textured draw carries the UV rect it
samples, and `tex_pack_on_textured_prim` sees all of them, so:

    python psxrecomp/tools/debug_client.py tex_pack sub=uvs

returns every distinct cell addressed so far, per `<texhash>-<palhash>`, in
texel coordinates relative to the texture page, plus the upload's origin in
that page. Subtracting the origin maps them 1:1 onto the dumped PNG's pixels.
Only populated while dumping, since it is an authoring aid.

Cells tile contiguously. In the F500 atlases they also leave exactly one blank
column, so `segment_atlas.py` recovers them from the bitmap alone and the UV
census is the cross-check — it agreed on every observed cell
(`cell = ink_start - 1 .. ink_end`). In the Arial-ish atlases the glyphs touch,
so the census is the only source, and the character order is confirmed by the
narrow glyphs landing where the typeface says they should (`i` and `l` 4 px,
`m` 13 px, `r` 6 px, `t` 5 px).

## Building a replacement

`build_font_atlas.py` takes a JSON config:

    {
      "src": "<dump>/75f5e38b-94f476b1.png",
      "out": "<replacements>/75f5e38b-94f476b1.png",
      "ttf": "F500 Ang-ular.ttf",
      "band_h": 18,
      "rows": ["abcdefghijklmnopqrstu", "vwxyz0123456789:./+-%", null]
    }

Per row it derives the scale and baseline from a reference glyph — the one
whose original ink height is the row's *modal* height, so a descender or a
short glyph cannot skew the fit — then draws each character so its rendered ink
box lands on the original's, times `SCALE` (16, giving 4096x896 to match the
existing HD pack's upscale factor). A `null` row is upscaled from the source
pixels instead: those are arrows and brackets with no counterpart in a text
typeface.

Ink colour and alpha are copied from the source atlas, so each palette
variant keeps its own colour and the CLUT's semi-transparency (alpha 127 on
these) survives. Anti-aliased edges modulate that alpha by coverage rather than
inventing an opaque edge.

## Typefaces

- **F500 Ang-ular** — the WipEout display face, from
  <https://github.com/NR74W/WipEout-Fonts> (archived). The file credits
  "F500 Ang-ular remake by NR74W (2021). Original font design by The Designers
  Republic (1999)." The repository carries no LICENSE file; see NOTICE.md.
- **Arimo** — SIL Open Font License, by Steve Matteson / Monotype, and
  *metrically compatible with Arial*, which is what the game's second face is
  derived from. `Arimo-OFL.txt` is its licence, kept here because it must ship
  with any redistribution.

## Status

The atlases these tools build are correct and land on the right cells (verified
in game: text rendered from a generated F500 atlas did not shift by a pixel).
They do not yet change what you see, because the runtime identifies replacement
textures but never substitutes them — `tex_pack.cpp` opens files only for
writing, and its "known" set is built by parsing filenames. Substituting the
pixels at draw time is the outstanding work, and it gates the third-party HD
texture pack too.
