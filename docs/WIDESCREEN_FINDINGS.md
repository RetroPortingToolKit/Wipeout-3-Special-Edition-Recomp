# Widescreen investigation — findings

Session date: 2026-08-16/17. Tested on `master` @ `97bd3d7`, psxrecomp `a1203df`,
recomp-ui `8e385a0`, OpenGL backend, WipEout 3 SE (PAL, SCES-02845).

Everything below was reproduced live. Where something is inferred rather than
observed it says so.

---

## 1. Headline: widescreen corrupts gameplay, not just rendering

With the widescreen mod enabled, the game misbehaves in ways that do not look
like rendering bugs:

- ship steering inverts / the craft flips opposite to the input
- enemy AI behaves wrongly
- large amounts of scenery appear and disappear as the camera moves

This is **not** the framerate/8 MB pacing work, and not the debug build. It was
reproduced on a plain `Release` build with **only** the widescreen mod enabled.

### The controlled result

`ws_margin` sets the cull-widening margin live over the debug server, with no
rebuild and no restart. Three states, one variable:

| State | Picture | Steering / AI | Scenery |
|---|---|---|---|
| widescreen off | 4:3 | correct | correct |
| widescreen on, `x_margin = 69` (shipped default) | 16:9 | **broken** | **large dropouts** |
| widescreen on, `x_margin = 0` | 16:9, *unchanged* | correct | correct |

The third row is the important one: the picture is still 16:9, so this
separates "widescreen looks wide" from "widescreen admits geometry
differently". Confirmed in both directions — 69 → broken, 0 → correct, 69 →
broken again.

### Mitigation available today

`ws_margin 0` is a working workaround: correct handling, correct scenery, and
the 16:9 image is retained. The cost is that geometry outside the original 4:3
bound is culled, so the far left/right of the frame can show missing scenery.
That is strictly better than the current default.

---

## 2. Mechanism

`psx_ws_x_margin()` reaches the guest through 68 emit sites. They do two very
different things.

**62 of them do not widen anything.** `psx_ws_cull_keep_result` is:

```c
return psx_ws_x_margin() > 0 ? (forced ? 1u : 0u) : vanilla;
```

The margin is consumed as a **boolean**: margins 1..255 are indistinguishable,
and only 0 restores the original comparison. Those sites are **clip-code
packers** — per-vertex screen-space tests such as `slti a1,a0,0` (guest
`0x8017032C`) and `slt a0,t6,a0` (`0x80170334`) — and while wide they are
pinned, i.e. the horizontal reject is **switched off**, not moved outward.

Pinning a clip classifier means polygons crossing the screen edge are never
clipped. They are submitted whole, with coordinates outside the PS1 GPU's legal
primitive range, and the hardware **discards the whole primitive**.

### Measurement supporting this

Sampling `gPrimCursor` during a race (same track, same camera path):

| | primitives emitted | arena used |
|---|---|---|
| `x_margin = 69` (broken) | ~323 | 12,956 B of 81,920 (15.8%) |
| `x_margin = 0` (correct) | ~761 | 30,476 B of 81,920 (37.2%) |

The broken configuration draws **less than half** as much geometry. Widening is
not adding geometry — it is destroying it downstream. Nothing overflows.

---

## 3. Corrections to existing documentation

### 3.1 `gPrimCursor` address was wrong (fixed in this repo)

`symbols.toml` recorded `0x801A8620`, and `game.toml`'s widescreen comment
repeated it. The instruction is `lui 0x801A` + offset `-31200`, i.e.
`0x801A0000 - 0x79E0` = **`0x80198620`**. A full-EXE scan finds **0** references
to `0x801A8620` and **116** to `0x80198620` (64 loads, 52 stores).

Anything written against the old address — a guard, a watchpoint, a `read_ram`
probe — silently watches a word the game never touches. (Observed: a live
`read_ram 0x801A8620` returns all zeros.)

### 3.2 "stomps racer state" is unverified, and the arena theory is disproved

`game.toml` asserted that surviving polygons advance the packet cursor until it
"stomps racer state". Measured, the arena peaks at **15.8%** occupancy while the
game is actively misbehaving. It is not overflowing.

Adjacency does not support the claim either: the arena is two `0x14000` halves
at `0x801A5944` / `0x801B9944` ending `0x801CD944`. Overrunning half 0 runs into
half 1 (the list being DMA'd); past half 1 sits a VRAM staging cursor and render
globals. No per-racer physics or AI array is adjacent in either direction.

### 3.3 Arena facts worth keeping

- Arena size **`0x14000` = 81,920 bytes** per buffer, double-buffered (3 in
  split view). Size is the `lui`/`ori` constant at `0x800EE474`.
- Reset once per frame at `0x800EE6A8` from `gPrimBufTable` (`0x801A4B60`),
  indexed by `0x80175CB0`.
- **No bounds check exists** at any of the 116 cursor accesses. Overflow, if it
  ever happened, would be silent.
- There are **three** unchecked bump cursors, not one. `0x801E5668` walks a hard
  **750-slot** 40-byte pool (bumped at `0x801701B0`); `0x801E5664` walks 190
  32-byte slots. Both were well under capacity in testing (212/750).

---

## 4. Issues for psxrecomp (framework)

### 4.1 `keep` is the wrong mechanism for clip-code sites

`docs/config_schema.md` already warns:

> *"Prefer an aspect-derived cone over `keep` when the original predicate is a
> camera-frustum test. `keep` has no queue policy and is appropriate only for a
> separately proven binary verdict."*

and, describing `aspect_cone`'s correctness property:

> *"A vanilla keep is always preserved. Only a vanilla rejection is retested."*

`keep` overrides the verdict in **both** directions, which is what breaks the
clipper here.

**What the fix needs.** The correct runtime semantics already exist:

```c
psx_ws_cull_slti_lower(sx, imm) = (int32_t)sx < imm - psx_ws_x_margin();
```

With `imm = 0` that gives `sx < -margin` — identity at margin 0, correctly
widened otherwise. What is missing is a way to **bind** these site addresses to
that helper. `auto_screen_x` only matches sites whose immediate is a screen
constant (`0x200`/`0x240`); these have immediate **0**, so it never matches them,
which is plausibly why `keep` was reached for.

Suggested: a new `[[widescreen.cull.widen]]`-style site list that maps an
address + expected instruction to the widening helpers (`slti_lower`, `bltz`,
and a register-bound form for `slt`), instead of pinning a result. The game repo
would then migrate its 62 `keep` entries.

*This is the piece we did not implement — it is a code-generator change and
CobaltCryptid may have context on why `keep` was chosen.*

### 4.2 `auto_screen_x` mis-detection (framework defect)

6 sites (`0x80110F6C`, `0x80110F74`, `0x8011178C`, `0x80111794`, `0x80141A48`,
`0x80141A50`) qualified for `auto_screen_x` only because the detector accepts a
function containing one W-immediate and one H-immediate *anywhere*
(`runtime/include/ws_cull_detect.h:68-82`), after which
`recompiler/src/code_generator.cpp:1178-1199` widens **every** W-immediate
compare in that function — including one that tests the same register as the
height compare.

Those particular funnels do not look like screen-X rejects at all: they bound a
persistent global cursor pair (`0x801A4B78` stepping +8, `0x801A4B8C` stepping
+32), so widening `x < 576` into `x < 645` moves an allocator bound. Sampled
live they read 704 and 224 and did **not** change with the margin, so they were
not the cause of the corruption here — but the detector heuristic is still
unsound.

### 4.3 `precise_nclip` is a silent no-op without PGXP

`gte_nclip` sources its high-precision coordinates from PGXP:

```c
if (gpu_ws_precise_nclip_enabled() &&
    pgxp_get_gte_sxy(0, &px0, &py0) && ...) { /* precise */ }
// else: rounded 16-bit SXY
```

With PGXP disabled the condition fails and it silently uses the rounded values.
So `precise_nclip = true` in a game.toml reads like protection is enabled when
it is not. Worth either documenting the dependency or warning at startup.

### 4.4 `screenshot_hires` emitted corrupted images — fixed, PR open

`handle_screenshot_hires` passed `out_pitch` in **pixels**; the resolves step
rows in **bytes** (as the present path does). Every capture came out tiled 4x
horizontally into the top quarter of the frame. Content rows measured 65/256
before, 256/256 after.

Same handler could also emit uninitialised heap at supersampling >= 2, because a
partial-cover resolve still returns non-zero.

Fixed in **mstan/psxrecomp#150**, which also adds `present_shot` (capture of the
composed present surface — every existing capture resolves *before* the
backend's aspect fit, so they answer 508x256 while the window shows 640x480;
that is fine for faithfulness work and wrong for anything aspect-shaped).

### 4.5 `ws_ui_groups` — new visibility command

`auto_ui_squash` squashes each spatial run of UI primitives about that run's own
anchor, so a HUD element split across two runs gets two anchors and comes apart
as the frame widens. Nothing exposed which run a primitive landed in: the group
key is a hash of CLUT/texpage/Y-band/family (so unequal keys do not say *which*
component differed), and the anchor takes only three values (so equal anchors do
not prove co-grouping).

`ws_ui_groups` dumps, per primitive, the op / key / raw key inputs / union-find
root / final anchor, plus frame-level `active`, `squash`, `dense`, `rank`,
`disp_x`, `disp_w`, `join_gap`.

This is directly relevant to CobaltCryptid's reported HUD bugs (bottom HUD
elements spreading apart; the lap-time seconds sliding off their white box).
Both are consistent with two primitives that belong together landing in
different runs — `family` separates polygons from rects/sprites, and `band`
splits on 24px boundaries, so a digit and its background box can never merge
even though they overlap.

---

## 5. Smaller gotchas worth knowing

- **`ntsc120full8` requires the 8 MB RAM mod.** Without it the game fail-fasts
  with `unknown dispatch: addr=0x000000B0`. The manifest documents the
  requirement ("Enhanced/Full need 8 MB RAM"); the runtime does not enforce it.
- **`frame_interpolation` in `settings.toml` is ignored on this title** — the
  runtime reports "Frame interpolation is mod-owned for this title; ignoring the
  legacy Settings value". So the framerate mod is the only route to 120 Hz here.
- **`ws_aspect` cannot A/B the squash on this title.**
  `refresh_widescreen_projection()` runs on every present and re-pushes the
  mod-selected aspect, overwriting it. `WIDESCREEN.md`'s suggested workflow
  ("toggle the GTE squash on/off in place") does not work here.
- **`WIDESCREEN.md:279-281` is stale**: it says native-wide is "software renderer
  only; hidden on OpenGL". The GL backend implements the full wide compositor;
  `gr_wide_supported()` is a per-backend vtable check. `renderer = "opengl"` is
  not why `native_wide` is disabled for this title. (The real reason —
  `gr_wide_clear` discarding the fill width and blanking the full wide-surface
  row — does check out.)
- **`--no-launcher` is required to use the TCP debug server.** With the launcher
  shown, `debug_server_init` has not run yet, so port 4370 is closed until PLAY.
- **Never hand anyone a `PSX_DEBUG_TOOLS=ON` build to play.** It compiles in
  per-block recording; measured here as roughly a 5x guest slowdown. Invisible at
  60 Hz, catastrophic with the 120 Hz mod.

---

## 6. How to reproduce

```bash
# build (Release, playable)
python psxrecomp/psxrecomp_cli.py ensure-emitters --project-root .
python psxrecomp/psxrecomp_cli.py generate --config game.toml --project-root . --disc <your.cue>
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target psx-runtime -j 16
```

Enable widescreen only (launcher Mods page, or `build-release/mods/state.toml`),
race a lap, and compare against the same build with the mod off.

For the live A/B you need the debug server, so build a second tree with
`-DCMAKE_BUILD_TYPE=RelWithDebInfo -DPSX_DEBUG_TOOLS=ON`, run it with
`--no-launcher`, and:

```bash
python psxrecomp/tools/debug_client.py ws_margin 0    # correct behaviour
python psxrecomp/tools/debug_client.py ws_margin -1   # back to shipped (69)
```

---

## 7. Open questions

- Why was `keep` chosen for the clip-code packers rather than a widening form?
  There may be context we are missing.
- The handling/AI corruption is *fixed* by `ws_margin 0`, but the causal path
  from "unclipped primitives discarded by the GPU" to "steering inverts" is not
  proven. Dropped rendering does not obviously explain inverted input. Something
  else may share the forced verdicts.
- Does the 8 MB work relocate the packet arena? If so, any future arena-derived
  guard needs per-configuration addresses rather than one hardcoded pair.
