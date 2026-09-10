# Semantic audit: `0x8015D68C` / `PutDrawEnv`

## Result

`0x8015D68C` is the PsyQ-compatible graphics-system routine `PutDrawEnv`:

```c
DRAWENV *PutDrawEnv(DRAWENV *env);
```

It compiles the requested drawing environment into the `DR_ENV` packet
embedded at `env + 0x1C`, submits that packet through the active GPU driver,
copies the full environment into the library's current-state cache, and
returns `env` unchanged.

## Exact boundary and ABI

- `0x8015D68C` opens a 32-byte frame and saves `ra/s0/s1/s2`.
- The only epilogue is `0x8015D734..0x8015D748`; `v0` is explicitly loaded
  from saved input `a0` before the restore.
- `0x8015D74C` begins the independent `PutDispEnv` routine with a new frame and
  a distinct retained diagnostic.

The exact range is `[0x8015D68C, 0x8015D74C)`, `0xC0` bytes, matching generated
metadata. The sole semantic argument is a pointer to a 0x5C-byte `DRAWENV`;
the routine assumes it is valid and performs no null check.

## Behavior and callees

1. When the graphics debug-level byte at `0x80178F4A` is at least 2, call the
   function pointer at `0x80178F44` with the retained format string at
   `0x8016F140` (`PutDrawEnv(%08x)...\n`) and the environment pointer.
2. Call `0x8015DE3C(env + 0x1C, env)`. Direct disassembly shows this helper
   converts the DRAWENV clip rectangle, draw offset, texture-window, mask,
   dithering, display-area, and background-clear fields into GPU command words
   in the embedded `DR_ENV` packet.
3. OR the packet tag word at `env + 0x1C` with `0x00FFFFFF`. This installs the
   standard end-of-ordering-table link while preserving the tag's high byte.
4. Load the active GPU driver object through `0x80178F40` and invoke its
   function pointer at `+0x08` as `(driver[+0x18], env + 0x1C, 0x40, 0)`. Thus
   exactly 0x40 bytes of compiled packet data are submitted.
5. Copy 0x5C bytes from `env` to the current DRAWENV cache at `0x80178F58`
   through the shared memory-copy routine `0x8015CF38`, then return `env`.

The embedded `PutDrawEnv` text, neighboring `PutDispEnv`, DRAWENV/DR_ENV field
layout, and return convention jointly make the SDK identity exact rather than
an inferred game-specific wrapper name.

## Direct callers

An exhaustive aligned JAL scan of the loaded executable finds exactly 20
calls, all in the graphics/system initializer:

```text
800EDD68 800EDD88 800EDDA4 800EDDB0 800EDDBC 800EDDC4 800EDDCC
800EDDD4 800EDDE4 800EDDEC 800EDDF4 800EDDFC 800EDE0C 800EDE14
800EDE1C 800EDE24 800EDE2C 800EDE34 800EDE3C 800EDE44
```

The initializer constructs 20 distinct 0x5C-byte environment objects and
passes each base address directly. There are no direct JALs to an internal
block of `PutDrawEnv`.

## Sources checked

- `disc/SCES_028.45`, respecting its 0x800-byte PS-X EXE header, for exact
  disassembly, retained strings, data references, and exhaustive caller scan;
- `generated/SCES_028.45_full.ranges` for the exact 0xC0 owner range;
- direct disassembly of compiler `0x8015DE3C`, adjacent `PutDispEnv`, and the
  system graphics initializer around the 20 call sites.
