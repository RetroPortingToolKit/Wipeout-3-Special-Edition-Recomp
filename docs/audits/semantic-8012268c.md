# Semantic audit: guest `0x8012268C`

## Conclusion

`0x8012268C` is the ABI-visible entry of the race-view frame renderer, not a
small DRAWENV helper. It initializes per-frame renderer state, dispatches the
world and vehicle drawing passes, submits effects and HUD work, performs the
GPU/display synchronization sequence, and returns after a large shared body.

Recommended symbol: `RenderRaceFrame` (`known`). The former `FrameDraw`
description was directionally correct but understated both the owner boundary
and the routine's responsibilities.

## Boundary and split ownership

The source-level owner starts at `0x8012268C` with a 376-byte stack frame. Its
matching restore/return is at `0x801247FC..0x80124828`; the next independent
prologue begins at `0x8012482C`. Thus the semantic extent is
`0x8012268C..0x8012482B` even though the generated range catalog divides it
into many control-flow pieces.

The first generated piece ends at `0x80122F84` by jumping to `0x80122FD0`.
Addresses such as `0x80122F88`, `0x80122FC0`, `0x80122FD0`, and
`0x801247FC` require the live 376-byte frame and saved registers established
at `0x8012268C`; they are alternate/internal continuations, not independent
callable routines. The restoration at `0x801247FC` exactly matches the entry's
saves of `ra`, `s8`, and `s0..s7`.

## Callers and effective ABI

There is one direct call in the retained executable: `0x801165A0`, inside the
larger race update/orchestration routine rooted at `0x80116128`.

The caller supplies:

- `a0 = *(0x801D1B7C)`;
- `a1 = *(0x801D1B80)`;
- `a2 = *(caller_sp + 0xD4)`.

At entry, `a0` is preserved in `s8`, while `a1` and `a2` are saved in the
incoming-argument area at new-SP `+0x17C` and `+0x180`. Proven uses make `a0`
a persistent view/player descriptor: the routine reads and writes fields such
as bytes `+5`, `+12`, and `+129`, and repeatedly passes the pointer to scene,
camera, and HUD helpers. `a1` and `a2` remain opaque render-context values;
they are forwarded to later passes, but the instruction stream does not yet
justify narrower source type names. No meaningful scalar return is consumed
by the sole caller; the useful results are global renderer state and GPU
command/display side effects.

## Frame responsibilities

The call sequence and state writes establish a complete race rendering pass:

1. Reset transient frame flags and packet/ordering-table state, publish the
   current view descriptor, and initialize camera/render globals.
2. Build camera and projection state and dispatch track/scenery, vehicle, and
   related scene-object passes through the `0x80131xxx`, `0x80132xxx`,
   `0x80134xxx`, `0x80141xxx`, and `0x80144xxx` renderer families.
3. Submit effects and race presentation layers, including repeated indexed
   object/HUD passes and the late `0x8015xxxx` effects/UI helpers.
4. Execute GPU/display synchronization calls in the final phase and select a
   display-buffer mode before joining the common continuation.
5. In that continuation, read the active DRAWENV rectangle's signed width and
   height at offsets `+4` and `+6`, publishing them to `0x80173A9C` and
   `0x80173AA0`. These globals are subsequently consumed as renderer clip
   bounds; this is one setup step, not the routine's whole purpose.
6. Complete the remaining HUD/display work and restore the owning frame at
   `0x801247FC`.

The many conditional branches are consistent with race modes, camera/view
variants, and optional presentation layers. Exact names for each individual
mode flag remain intentionally unresolved.

## Confidence and unresolved detail

- **High:** callable entry, sole direct caller, 376-byte owner frame, split
  extent through `0x80124828`, persistent `a0`, DRAWENV bound publication,
  scene/HUD submission, and late GPU/display synchronization.
- **Medium:** `view/player descriptor` as the source-level type description and
  `race` as the mode name. The unique caller is in the gameplay update path and
  the callee invokes the known renderer families, but original type names are
  unavailable.
- **Intentionally unresolved:** exact types of `a1`/`a2`, meanings of every
  descriptor field and mode branch, and names of the individual subordinate
  render passes.

## Primary evidence

- `generated/SCES_028.45_full_112.c`: sole call at `0x801165A0` and argument
  setup
- `generated/SCES_028.45_full_116.c`: entry, frame setup, first rendering
  phases, split pieces, and DRAWENV width/height publication
- `generated/SCES_028.45_full_117.c`: later rendering phases and matching
  restore/return at `0x801247FC..0x80124828`
- `generated/SCES_028.45_dispatch.c`: generated split ownership and dispatch
  entries
