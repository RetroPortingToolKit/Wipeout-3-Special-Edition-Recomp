# Clean modern-PC application shell

## Production graphics path

`RuntimeGraphics` is the live application-owned bridge from the runtime GPU to
host presentation. In production scanout mode it drains the bounded native GPU
event queue into deterministic raster VRAM, derives scanout exclusively from
GP1 display timing, decodes RGB555 or packed 24-bit VRAM, and submits the result
to the host in the same update. The default remains exact 4:3; 16:9, 21:9,
32:9, and custom presentation are explicit configuration choices.

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
shell contains no compiler, decoder, interpreter, JIT, capture path, game
identity shortcut, or fallback dispatch.

`Application` is platform-neutral and drives one authenticated generated
region, deterministic runtime advancement, frame submission, exact 441-frame
audio batches, controller state, and bounded host service per update.
`SdlPlatform`, enabled with `-DCLEAN_AOT_APP_ENABLE_SDL=ON`, is the minimal
native adapter: a resizable high-DPI window, SDL audio stream, bounded event
pump, and first-gamepad sampling. It deliberately has no emulator menus.

The test target generates a tiny synthetic catalog, receipts, executable,
artifact, BIOS, and native region. Passing it proves the complete application
boundary without fabricating BIOS or game boot.
