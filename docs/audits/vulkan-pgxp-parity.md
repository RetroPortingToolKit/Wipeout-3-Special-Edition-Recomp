# Vulkan / PGXP / HD parity audit

Date: 2026-08-22  
Scope: the configured WipEout 3 SE feature set, the GooseStation/DuckStation
backend surface described by the checked framework, and the current
`psxrecomp` product integration. This is an evidence audit; it does not alter
renderer, PGXP, generated, or launcher code.

## Finding

The configured feature set is OpenGL-only in the sense that the shipped game
profile selects `renderer = "opengl"` while enabling geometry correction,
perspective texturing, PGXP depth, 4x supersampling, and HD replacements
(`game.toml:47-58`). The build selects the PGXP runtime (`CMakeLists.txt:37-40`).

That configuration does not establish that Vulkan silently falls back. It
establishes only that Vulkan is not the product-selected backend. Existing
renderer evidence says the framework accepts software, OpenGL, and Vulkan, and
that an unavailable selected backend can fall back to software; no WipEout
Vulkan launch receipt records which path was initialized. A requested Vulkan
run must therefore be considered **unproven**, not “equivalent” and not
automatically a fallback.

## Capability matrix

| Capability | OpenGL evidence | Vulkan disposition |
| --- | --- | --- |
| Backend selection | Product profile selects OpenGL; release capture exists | Build/config surface exists, but no WipEout launch receipt |
| PGXP geometry / perspective texturing | Configured and exercised on the demonstrated path | No backend-specific runtime receipt |
| PGXP depth | `pgxp_depth = true`; OpenGL depth/stencil capture exists | Source/config indication is insufficient; occlusion and reset are untested |
| HD replacement binding | Existing audit identifies OpenGL `set_replacement` binding and live pack hits | Existing audit identifies no Vulkan `set_replacement` implementation; replacement parity is therefore absent or unverified, not a silent success |
| Widescreen interaction | WipEout-owned wide/cull path is configured; 21:9 safety remains open | No Vulkan wide/depth/PGXP capture |
| Fallback reporting | Framework facade can fall back to software when unavailable | No title-specific receipt proving whether a Vulkan request initializes, falls back, or fails |

The HD audit is the strongest direct parity signal: the replacement tracker is
backend-agnostic, but only OpenGL exposes the binding hook; Vulkan and software
do not provide `set_replacement` in the inspected framework revision. Thus a
Vulkan request cannot be credited with OpenGL HD replacement behavior. Depth
resources may exist in Vulkan, but resource existence is not proof that PGXP
coordinates, depth writes/tests, HUD ordering, or reset semantics match.

## GooseStation comparison

The relevant GooseStation/DuckStation model is a shared hardware-renderer
feature surface with separately selectable OpenGL/Vulkan (and, upstream,
additional platform backends), plus independent PGXP-related geometry, depth,
texture, culling, and state controls. WipEout exposes a smaller umbrella:
geometry correction, perspective texturing, and depth are enabled together;
it does not expose per-backend capability reporting or a Vulkan-specific HD
replacement contract. This is a backend parity gap, not evidence that the
title needs another backend added now.

## Verification boundary

Run the bounded static check with:

```text
python tools/verify_vulkan_pgxp_parity.py
```

It verifies the product PGXP/OpenGL/HD configuration. If the framework
submodule is absent, it reports `UNKNOWN` and deliberately does not invent
source evidence. If populated, it checks the OpenGL/Vulkan replacement-hook
surface and still reports runtime parity as unproven. It is not a substitute
for a real Vulkan launch.

## Required closure evidence

Before exposing Vulkan as a supported WipEout option, capture the same fixed
menu, FMV, start-grid, and race route under OpenGL and Vulkan with HD on/off.
Record the initialized backend (and any fallback), PGXP/depth behavior,
replacement hits/misses and uploads, widescreen/culling stability, and actual
swap cadence. A software fallback must be reported as a failed Vulkan parity
case, not as a passing control.

No broad runtime edits are justified by this audit. The safe current claim is:
**OpenGL is the only configured and evidenced full PGXP/HD path; Vulkan is a
build/framework capability with missing replacement and runtime parity proof.**
