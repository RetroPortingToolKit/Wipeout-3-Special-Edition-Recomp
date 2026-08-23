# WipEout 3 SE HD texture/font replacement audit

Audit date: 2026-08-22

Scope: external pack discovery, Beetle replacement-key compatibility, VRAM
tracking and lookup, decoded-image/GL upload caching, title and boot text,
font atlases, Psygnosis logo assets, missing sidecars, and release packaging.
This is an evidence audit only. No runtime, GPU, PGXP, launcher, build-system,
game configuration, active-mod, or generated-overlay files were changed.

## Result at a glance

The replacement path is wired end to end for the OpenGL renderer and has live
evidence of substitution. It is not yet a complete asset/release contract:

- Automatic discovery selects the external pack beside the stock disc and the
  runtime indexes all 445 PNG entries. The current log proves discovery, not
  that every entry was drawn.
- The strongest existing Debug RelWithDebInfo run recorded 445 indexed entries,
  299 matched entries, 300 decoded images, 47,919 replacement hits, and zero
  decode failures. The repository handoff explicitly limits that proof to the
  observed 299 entries.
- The source Wip3out pack is partial. Its manifest records 98 missing mappings,
  and all 98 keys are still absent from the current generated
  `fullcoverage-scale2x-8x` pack. Exact keys and logical asset paths are listed
  below.
- The current pack contains the title Psygnosis text/logo files and the font
  atlas PNGs. However, `exclude.txt` contains only commented font hashes, and
  neither `game.toml` nor the staged `build-release/game.toml` contains an
  active `hd_texture_exclude` entry. Font substitution is therefore enabled by
  the checked-in release configuration.
- `coverage.json` is a last-session report, not a pack manifest. The copy
  currently beside the disc records `pack_entries: 445`, `matched: 0`, and
  `unmatched: 445` after a 205-second session. It must not replace the earlier
  live-hit evidence or be read as proof that the pack cannot match.
- The portable-release checks validate `hd_textures = true` and the launcher
  assets, but do not require, hash, or exercise the external pack. The runtime
  deliberately keeps that pack outside the executable release tree.

## 1. Asset sources and inventory

### Upstream authored pack

Source checkout:

`C:\Users\Shokunin\Documents\ChatGPT\wipeout3se-sources\Wip3out-HD-Texture-Pack`

Reference files:

- `Hashes.ini`: 292 PNG payloads, 390 mapping lines, 98 missing mappings.
- `README.md`: upstream installation/developer notes.
- Source repository: `princepeter64/Wip3out-HD-Texture-Pack`.
- Source commit: `039a0690448047401cef251594327a25fcc73fad`.
- `Hashes.ini` SHA-256:
  `9cfb23b92d72bc1233a76958621ddae7ab049493a7d2651fdd1a12e1455e5c71`.
- `README.md` SHA-256:
  `4f45f9a19adc526780fdd45cb740218f1eb84bbf51d3e6430049328cf049bee5`.

The cleanroom manifest at
`C:\Users\Shokunin\Documents\ChatGPT\wipeout3se-cleanroom\assets\manifests\wip3out-hd-textures.json`
is the structured source of the missing-key list. Its status is
`valid-partial-pack`; it does not claim that the 292 authored images cover all
390 logical mappings.

### Pack actually selected by the runtime

`C:\Users\Shokunin\Documents\ChatGPT\wipeout3se\disc\WipEout 3 - Special Edition (Europe) (En,Fr,De,Es,It)-texture-replacements-fullcoverage-scale2x-8x`

Observed inventory:

- 445 `.png` files indexed by the runtime.
- `build_report.json`, `coverage.json`, and `exclude.txt` are present but are
  not PNG replacement entries.
- No invalid filenames, duplicate basenames, or zero-byte PNGs were found.
- PNG bytes: 4,528,241. Total directory bytes after generation:
  5,080,385.
- The generation report says 439 dump pairs were covered (100%), growing the
  selected pack from 292 to 445 PNG entries. It records 400 generated missing
  entries, 33 authored byte copies, 4 protected F500 copies, 6 protected F500
  extras, and 2 rederived nearest-neighbour entries.
- All 445 selected outputs passed the report's alpha/RGB/RGBA checks and the
  4096-axis limit. Fourteen font-related outputs have a 4096-pixel axis; this
  is legal for the pack generator but is material to minification/filtering.

The generated pack adds coverage relative to the dump, but it does not fill
the 98 upstream mapping gaps below: a key comparison found 0 of those 98 keys
in the current 445-file directory.

## 2. Discovery and replacement-key contract

The checked-in configuration has:

```toml
[video]
renderer = "opengl"
hd_textures = true
```

The runtime's effective precedence is `game.toml`, then user `settings.toml`,
then environment overrides. `PSX_HD_TEXTURE_PACK` is an explicit pack-folder
override. With no explicit override, discovery uses the resolved stock disc,
not a modded disc:

1. Look beside the disc for the exact `<disc-stem>-texture-replacements`
   directory.
2. If it is absent, scan only that same disc directory for directories whose
   names start `<disc-stem>-texture-replacements-`.
3. Choose the lexicographically smallest matching variant.
4. Do not search outside the disc directory and do not copy the pack into the
   executable release tree.

The observed startup line in
`build-release/audit-current-1787.out.log` is:

```text
[tex_pack] replace=1 dump=0 pack=...-texture-replacements-fullcoverage-scale2x-8x (445 entries) dump_dir=...-texture-dump
```

That confirms the selected folder and the filename index. It does not confirm
that a primitive's live VRAM upload, UV rectangle, palette, and renderer all
matched a replacement.

The on-disk contract is the Beetle PSX HW convention:

```text
<disc-stem>-texture-replacements/<texhash>-<palhash>.png
```

Both hashes are lowercase hexadecimal with no padding. `texhash` is the CRC-32
of the uploaded 16-bit VRAM rectangle in row-major order; `palhash` is the
CRC-32 of the live 4bpp/8bpp CLUT row, or zero for 15bpp. The implementation
documents the Beetle polynomial, seed/final XOR, wrapped-X upload hashing, and
UV-limited sampling contract in `psxrecomp/runtime/include/tex_pack.h`.

### What is and is not a match

The runtime has two intentionally different notions of matching:

- `pack_matched`/`matched` is an authoring census. A textured primitive can
  mark a known key on overlap.
- `repl_hit` is actual substitution. Lookup scans live uploads newest-first and
  requires the primitive's sampled VRAM rectangle to be contained by the upload.
  It then requires the exact texture/palette key, or a safe single-colour mask
  donor for palette recolouring.

This distinction explains why `299 matched` is not the same as `445 entries`
and why `coverage.json` cannot be used as a decoded-image count. A replacement
can be indexed and authoring-matched without being armed on the live draw.

## 3. Upload tracking, decode cache, and renderer upload

The path is:

1. `gpu_render.c` observes CPU-to-VRAM uploads, VRAM copies, fills, and direct
   invalidations through the renderer facade.
2. Uploads are tracked by rectangle and CRC. Any overlapping non-upload write
   kills the tracked upload in the current v1 lifetime model; copies are
   resolved lazily on first sampled use.
3. Textured triangles and rectangles derive UV limits, texture depth, CLUT, and
   texture page, then call the replacement lookup.
4. The lookup memo is keyed by UV limits, CLUT, texture page, and mutation
   marks. New uploads are checked lazily for intersection with the sampled
   rectangle or CLUT before a memoized result is reused.
5. A matching PNG is decoded lazily on first use with `stb_image` to RGBA8.
   Decode failures are cached, so a broken PNG is not reopened every frame.
6. The OpenGL backend creates one texture name per decoded replacement,
   uploads it with `glTexImage2D`, caches the name through `gl_handle`, and
   arms it for one textured primitive. `CLAMP_TO_EDGE` and linear filtering are
   used; the replacement is then disarmed.

Only OpenGL currently exposes the replacement binding hook. The tracker is
backend-agnostic, but the Vulkan and software renderer files do not provide a
`set_replacement` implementation. `game.toml` selecting OpenGL is therefore a
functional requirement, not merely a preference.

There is an implementation/documentation mismatch worth retaining in the
release risk list: the OpenGL comment says the replacement texture must be
mipmapped for minified font strokes, but the observed upload path sets
`GL_LINEAR` min/mag filters and does not call `glGenerateMipmap` or upload mip
levels. This audit does not change that runtime code.

## 4. Fonts, boot/title text, and Psygnosis assets

### Font atlas keys

The upstream `Hashes.ini` identifies game text as PNG atlas replacements, not
as TrueType/OpenType sidecars. The current generated pack contains all of the
following font-key families:

| Family | Exact replacement keys |
| --- | --- |
| Main-menu F500 | `3dc173e1-3da5d410`; `4cb2ec4e-3da5d410`; `4cb2ec4e-7205c45b`; `4cb2ec4e-94f476b1`; `75f5e38b-3da5d410`; `75f5e38b-7205c45b`; `75f5e38b-94f476b1` |
| Main-menu Helvetica | `6b411011-273d3b40`; `861ad9f1-273d3b40`; `861ad9f1-5888b400`; `d20dee94-273d3b40`; `d20dee94-5888b400` |
| In-game F500 | `3dc173e1-7205c45b`; `3dc173e1-8ec51178`; `3dc173e1-94f476b1`; `3dc173e1-41e26c01`; `4cb2ec4e-b6e6caeb`; `75f5e38b-b6e6caeb`; `4cb2ec4e-8ec51178`; `75f5e38b-8ec51178` |

The current pack's `exclude.txt` says font atlases are held back, but the only
hash lines are:

```text
# d20dee94
# 861ad9f1
```

The leading `#` makes both lines comments; the parser ignores them. The file
also does not list the F500 hashes `4cb2ec4e` and `75f5e38b`. The checked-in and
staged `game.toml` files have no `hd_texture_exclude` setting. Consequently,
there is no active font-atlas exclusion in the audited release state. This is
the direct explanation for why font replacement remains capable of producing
the previously observed invisible/doubled-stroke text failure even though the
pack contains the font PNGs.

There is no missing game-font sidecar to add: the replacement loader consumes
PNG files named by texture/palette key. The `.ttf`/font files under
`assets/fonts` are launcher UI assets and are checked by the portable-release
layout script; they are not used to render WipEout's in-game text. The two JSON
files in the external pack are build/coverage metadata, not required font
sidecars, and the runtime does not validate them before indexing PNGs.

### 2000 Psygnosis/logo text

The `©2000 Psygnosis Ltd :: F7200 Racing League` line in `Hashes.ini` is source
provenance, not a separate key containing `2000`. The actual title-screen
assets are:

| Key | Logical asset | Current pack |
| --- | --- | --- |
| `2a5e8698-cd57d48a` | `Main Menu/Title Screen/Psygnosis Logo Text.png` | present |
| `bfd5825a-712aa77d` | `Main Menu/Title Screen/Psygnosis Logo.png` | present |

Other title-screen text/button keys are also present in the current pack:
`e8a8dde-322b5399`, `8cb24699-7859d502`, `421fdb44-7859d502`,
`eb59bd18-cd57d48a`, `415f3330-cd57d48a`, `a154479a-cd57d48a`,
`c2eb1e8b-cd57d48a`, `5fe9e228-cd57d48a`, `bd7f89fa-cd57d48a`, and
`65827f71-cd57d48a`. No separate `2000` logo-text key or sidecar exists in
the audited source mapping.

## 5. Exact missing source assets

The following 98 rows are the exact `missing_mappings` entries in
`wip3out-hd-textures.json`. The reason is the same for every row: the mapping
exists in `Hashes.ini`, but the corresponding PNG payload is absent from the
292-file source pack, and the key is also absent from the current 445-file
full-coverage directory. These are source-pack asset gaps, not proof that the
runtime's current session failed to look them up.

| Key | Logical asset path |
| --- | --- |
| `17967c70-a45085b7` | `In Game/Billboards/18 - LS102-MG71/AG-S.png` |
| `17970de9-cbccbc4` | `Main Menu/Team/Ship/8 - Icaras/Droid.png` |
| `18698b0-836168c4` | `In Game/Billboards/18 - LS102-MG71/Assegai.png` |
| `18e446ff-d7224eb2` | `Main Menu/Team/Ship/6 - AG-S/Drag.png` |
| `190425d-7e2a304a` | `Main Menu/Team/Ship/7 - Assegai/Standard/Frame1.png` |
| `1a96f88a-d1c7a23c` | `In Game/Ships/3 - Auricom/VR/Grey.png` |
| `1abf97a6-14b7a9e0` | `In Game/Ships/2 - Goteki 45/VR/Pastel Orange.png` |
| `1ce62a77-8ea1b423` | `Main Menu/Team/Ship/3 - Auricom/Standard/Frame3.png` |
| `1da23834-3305166` | `In Game/Billboards/20 - LS104-AW73/Goteki 45.png` |
| `1e1b3e5e-125f678d` | `Main Menu/Team/Ship/1 - FEISAR/Droid.png` |
| `1e680ea0-8a735387` | `In Game/Billboards/19 - LS103-AW72/Pirhana.png` |
| `23be67e0-c006fee8` | `Main Menu/Team/Ship/7 - Assegai/Standard/Frame2.png` |
| `2519ab24-69e14b88` | `In Game/Billboards/18 - LS102-MG71/Icaras.png` |
| `29a07dd-cd19c33c` | `Main Menu/Team/Ship/7 - Assegai/Droid.png` |
| `29ab835-9395e44d` | `In Game/Billboards/18 - LS102-MG71/Goteki 45.png` |
| `2e206169-ef35d018` | `In Game/Ships/3 - Auricom/VR/Blue.png` |
| `2feeb4bd-d39cb3d2` | `In Game/Ships/3 - Auricom/VR/Blue-Grey 1.png` |
| `30a03480-6513d5e1` | `In Game/Billboards/17 - LS101-NP62/Pirhana.png` |
| `31d0a5e4-3b4cdd97` | `Main Menu/Team/Ship/4 - Pirhana/Standard/Frame1.png` |
| `327cdef0-470e1cb4` | `Main Menu/Team/Ship/8 - Icaras/Standard/Frame2.png` |
| `32aed7f2-13647f70` | `Main Menu/Team/Ship/3 - Auricom/Standard/Frame2.png` |
| `32ca58d6-141416b` | `Main Menu/Team/Ship/2 - Goteki 45/Drag.png` |
| `371d0f7d-4a108815` | `Main Menu/Team/Ship/2 - Goteki 45/Standard/Frame2.png` |
| `3f930daa-4a59c2e1` | `In Game/Ships/5 - Qirex/VR/Cyan.png` |
| `41685e31-a02f57e6` | `Main Menu/Team/Ship/5 - Qirex/Drag.png` |
| `41865946-fa25ad0a` | `In Game/Tracks/Rescue Droid/Part 3.png` |
| `439166ea-cade23ae` | `In Game/Tracks/Rescue Droid/Part 1.png` |
| `44b88a66-b5285ab1` | `In Game/Billboards/19 - LS103-AW72/AG-S.png` |
| `47cc70e0-732c03af` | `Main Menu/Team/Ship/4 - Pirhana/Drag.png` |
| `4f6afec4-bcb03d85` | `In Game/Ships/4 - Pirhana/VR/Red.png` |
| `53574549-ca822682` | `Main Menu/Team/Ship/2 - Goteki 45/Droid.png` |
| `59463d82-6396013b` | `In Game/Ships/8 - Icaras/VR/Dark Pink.png` |
| `5972482c-659517d2` | `In Game/Billboards/17 - LS101-NP62/Icaras.png` |
| `5c06bff8-88147a97` | `In Game/Ships/6 - AG-S/VR/White.png` |
| `6337b31f-8b9142f8` | `In Game/Ships/1 - FEISAR/VR/Turquoise.png` |
| `655d047a-4fde5fa` | `In Game/Billboards/20 - LS104-AW73/Icaras.png` |
| `68f17c9d-77998795` | `Main Menu/Team/Ship/5 - Qirex/Standard/Frame2.png` |
| `6b34a406-13f7d6af` | `Main Menu/Team/Ship/1 - FEISAR/Standard/Frame2.png` |
| `6b7038cd-ea742122` | `Main Menu/Team/Ship/1 - FEISAR/Drag.png` |
| `6f3fec58-f5fb608e` | `In Game/Billboards/19 - LS103-AW72/The Designers Republic.png` |
| `755a8917-9aed1d59` | `Main Menu/Team/Ship/4 - Pirhana/Standard/Frame3.png` |
| `7776aa4f-f571dd3a` | `Main Menu/Team/Ship/7 - Assegai/Drag.png` |
| `791b0d5e-4e826174` | `Main Menu/Team/Ship/1 - FEISAR/Standard/Frame1.png` |
| `7c3085ca-2a9e7d32` | `In Game/Billboards/17 - LS101-NP62/The Designers Republic.png` |
| `7d8539ed-d0d35b9e` | `Main Menu/Team/Ship/6 - AG-S/Droid.png` |
| `81cdf3a1-4346addf` | `In Game/Billboards/20 - LS104-AW73/Pirhana.png` |
| `830c0a85-ab97002e` | `Main Menu/Team/Ship/4 - Pirhana/Standard/Frame2.png` |
| `83edb773-bc449e6d` | `In Game/Billboards/20 - LS104-AW73/AG-S.png` |
| `8465b60-990ddc2` | `Main Menu/Team/Ship/3 - Auricom/Droid.png` |
| `85fdc353-29f543bf` | `In Game/Billboards/20 - LS104-AW73/FEISAR.png` |
| `8821d431-237d44ee` | `In Game/Ships/6 - AG-S/VR/Red.png` |
| `8821d431-7d049ef2` | `In Game/Ships/7 - Assegai/VR/Khaki.png` |
| `88c04895-b0cfe605` | `Main Menu/Team/Ship/8 - Icaras/Standard/Frame1.png` |
| `96a9d2e6-81e343cb` | `Main Menu/Team/Ship/3 - Auricom/Standard/Frame1.png` |
| `9712466-6c6f25c` | `In Game/Tracks/Rescue Droid/Part 2.png` |
| `974c76ce-84fb38b1` | `Main Menu/Team/Ship/6 - AG-S/Standard/Frame2.png` |
| `978d8556-9121c5bb` | `In Game/Billboards/17 - LS101-NP62/FEISAR.png` |
| `a12340c5-e708a24e` | `Main Menu/Team/Ship/1 - FEISAR/Standard/Frame3.png` |
| `a7186b82-62aa978b` | `In Game/Ships/1 - FEISAR/VR/Cyan-Grey.png` |
| `a9f9c1de-79606c05` | `Main Menu/Team/Ship/6 - AG-S/Standard/Frame1.png` |
| `aa8fd01-822caac7` | `In Game/Billboards/18 - LS102-MG71/Pirhana.png` |
| `ab0782f4-45ae8b48` | `In Game/Tracks/Rescue Droid/Propeller 3.png` |
| `b5236a84-99ea20a0` | `In Game/Ships/4 - Pirhana/VR/Yellow.png` |
| `b6ed0a89-55f2e9a` | `Main Menu/Team/Ship/5 - Qirex/Droid.png` |
| `b6f3d7ec-19ec6904` | `In Game/Billboards/17 - LS101-NP62/Goteki 45.png` |
| `ba625780-5387b4fb` | `In Game/Ships/1 - FEISAR/VR/Yellow.png` |
| `bbfd1839-13ca6419` | `In Game/Billboards/17 - LS101-NP62/AG-S.png` |
| `bc1c1ad0-9085e5c1` | `Main Menu/Team/Ship/8 - Icaras/Drag.png` |
| `be29d9e8-d35dede7` | `In Game/Tracks/Rescue Droid/Propeller 4.png` |
| `c0dd79d4-eacae820` | `Main Menu/Team/Ship/3 - Auricom/Drag.png` |
| `c2826a85-32a6075a` | `Main Menu/Team/Ship/4 - Pirhana/Droid.png` |
| `c5e357d0-f3e7211d` | `In Game/Ships/3 - Auricom/VR/Blue-Grey 2.png` |
| `cedfcf83-55de3a01` | `In Game/Ships/8 - Icaras/VR/Dark Orange.png` |
| `cf5317e5-8710ac59` | `Main Menu/Team/Ship/5 - Qirex/Standard/Frame3.png` |
| `d053154b-dc368212` | `In Game/Billboards/17 - LS101-NP62/Assegai.png` |
| `d343c167-59e5424e` | `In Game/Ships/8 - Icaras/VR/Light Pink.png` |
| `d3676a9-f51a355` | `In Game/Ships/5 - Qirex/VR/Purple.png` |
| `d7ef67ed-6e9a1a9f` | `In Game/Billboards/18 - LS102-MG71/The Designers Republic.png` |
| `d7ef67ed-b40e87cd` | `In Game/Billboards/20 - LS104-AW73/The Designers Republic.png` |
| `d8457c49-f8e6ed40` | `Main Menu/Team/Ship/6 - AG-S/Standard/Frame3.png` |
| `d84cc5f3-de0692ab` | `In Game/Tracks/Rescue Droid/Part 5.png` |
| `e53c49be-330e59b5` | `In Game/Billboards/20 - LS104-AW73/Auricom.png` |
| `e5e06b8e-124922da` | `Main Menu/Team/Ship/7 - Assegai/Standard/Frame3.png` |
| `e7e6e922-45ae8b48` | `In Game/Tracks/Rescue Droid/Propeller 2.png` |
| `e7ecb387-a90390ea` | `In Game/Billboards/18 - LS102-MG71/Auricom.png` |
| `e80579b1-7671840a` | `In Game/Ships/7 - Assegai/VR/Khaki-Cyan.png` |
| `e8c7ab77-d5cc51b0` | `Main Menu/Team/Ship/2 - Goteki 45/Standard/Frame3.png` |
| `ea286c63-cee6761c` | `In Game/Ships/8 - Icaras/VR/Dark Orange-Dark Pink.png` |
| `eda9c80d-6175ad96` | `In Game/Billboards/18 - LS102-MG71/FEISAR.png` |
| `f11c9ba8-216e833a` | `In Game/Billboards/19 - LS103-AW72/Goteki 45.png` |
| `f1b8d574-bc1a24e8` | `In Game/Billboards/17 - LS101-NP62/Auricom.png` |
| `f1ba9531-e0400610` | `Main Menu/Team/Ship/2 - Goteki 45/Standard/Frame1.png` |
| `f3c3a387-5044d6a4` | `In Game/Ships/1 - FEISAR/VR/Turquoise-Yellow.png` |
| `f40a7c9f-31cf5e8a` | `Main Menu/Team/Ship/8 - Icaras/Standard/Frame3.png` |
| `f58b7896-45ae8b48` | `In Game/Tracks/Rescue Droid/Propeller 1.png` |
| `f690a15a-b5b41ff0` | `Main Menu/Team/Ship/5 - Qirex/Standard/Frame1.png` |
| `fccd643c-2d642c6b` | `In Game/Tracks/Rescue Droid/Part 4.png` |
| `fd9b8f70-d9effc87` | `In Game/Billboards/19 - LS103-AW72/Assegai.png` |

The missing rows group into 40 main-menu ship assets, 28 billboards, 21 VR
ship variants, and 9 rescue-droid parts/props. None is a font, title-logo, or
Psygnosis-text mapping.

## 6. GooseStation/DuckStation comparison

The local vendored reference is
`C:\Users\Shokunin\Documents\ChatGPT\wipeout3se-sources\duckstation-a233ec1`,
with GooseStation integration patches in
`C:\Users\Shokunin\Documents\ChatGPT\wipeout3se-sources\goosestation-builder\goosify.sh`.

The reference confirms the expected lifecycle concepts:

- Track guest texture uploads/VRAM writes before replacement lookup.
- Index replacement files separately from decoded CPU images and GPU images.
- Optionally preload replacement images to reduce first-use stutter.
- Purge or reload replacement caches when the game serial or replacement
  settings change.
- Apply replacements as source sub-images with explicit destination rectangles,
  scale, and transparency semantics.

It is not a drop-in filename-format reference for this pack. The inspected
DuckStation core expects a serial-scoped texture directory and filenames such
as `texupload-...` and `texpage-...`, carrying 64-bit source/palette hashes,
texture mode, source dimensions, offsets, and replacement rectangles. WipEout's
pack intentionally uses the simpler Beetle-compatible 32-bit
`<texhash>-<palhash>.png` contract. GooseStation's
`goosestation_gpu_texture_cache` and preload settings enable the reference
cache behavior, but they do not create a bridge from DuckStation's filename
grammar to the WipEout pack.

## 7. Release packaging and acceptance boundary

`tools/verify_static_release.ps1` checks the staged configuration for
`hd_textures = true`, OpenGL/4x-related release invariants, no OpenBIOS, and no
overlay/cache artifacts. It does not check for an external pack, its 445 PNGs,
the source manifest hash, `exclude.txt`, `coverage.json`, or any decoded hit.

`tools/verify_one_folder_release.ps1` checks the executable, `assets/fonts`,
`assets/img`, bezels, active packages, and absence of developer BIOS/disc/user
settings sidecars. It likewise does not package or validate the external HD
directory. This is consistent with runtime discovery: the pack is user-owned
content next to the selected disc and is intentionally not copied into the
portable executable folder.

Therefore the release currently has two separate contracts:

1. The portable executable layout can pass without HD content.
2. HD visuals work only when the user supplies the correctly named external
   pack, launches the OpenGL renderer, and has active font-exclusion policy if
   font substitution is not wanted.

For a release claim of complete HD coverage, the acceptance record should
retain the selected pack path and 445-entry index line, a full content run with
`repl_hit`, `repl_decoded`, and `repl_failed`, explicit title/font/logo key
observations, and a pack validation that is independent of the mutable
last-session `coverage.json`. None of those release-gate changes were made in
this audit.

## Evidence index

- Runtime configuration: `game.toml` and staged `build-release/game.toml`.
- Discovery log: `build-release/audit-current-1787.out.log`.
- Prior live substitution summary: `docs/HANDOFF.md`, lines 17-19 and 289-290.
- Replacement contract/tracker: `psxrecomp/runtime/include/tex_pack.h` and
  `psxrecomp/runtime/src/tex_pack.cpp`.
- Renderer bridge: `psxrecomp/runtime/src/gpu_render.c` and
  `psxrecomp/runtime/src/gpu_gl_renderer.c`.
- Pack generation evidence: external `build_report.json`, `exclude.txt`, and
  `coverage.json` beside the selected disc.
- Source mapping evidence: external `Hashes.ini` and the cleanroom
  `wip3out-hd-textures.json` manifest.
- Reference behavior: vendored DuckStation `src/core/gpu_hw_texture_cache.cpp`
  and GooseStation `goosify.sh`.
