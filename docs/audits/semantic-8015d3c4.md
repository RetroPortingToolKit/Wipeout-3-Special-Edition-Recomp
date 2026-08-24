# Semantic audit: `0x8015D3C4` / `ClearImage`

## Result and ABI

`0x8015D3C4` is the canonical PsyQ GPU wrapper `ClearImage`:

```c
int ClearImage(const RECT *rectangle, uint8_t r, uint8_t g, uint8_t b);
```

It passes the rectangle through the shared GPU diagnostic helper, packs the
three color bytes, dispatches clear operation 8 through the active GPU driver,
and propagates the backend's scalar result unchanged.

## Boundary and control flow

The generated range `[0x8015D3C4,0x8015D454)` is exact, length `0x90`. The
entry creates a 40-byte frame and saves `ra` plus `s0..s3`; the sole epilogue
returns at `0x8015D44C` and restores the frame in its delay slot. The next
independent wrapper begins at `0x8015D454` with a new frame. No direct jump or
JAL targets an interior instruction.

The four arguments arrive conventionally in `a0..a3`. RGB is explicitly
truncated to eight bits and packed as:

```text
packed_color = (b << 16) | (g << 8) | r   # 0x00BBGGRR
```

The wrapper itself does not dereference the rectangle or touch GPU MMIO.

## Diagnostic and backend dispatch

First it calls the shared GPU diagnostic/validation helper at `0x8015D2A8`
with retained operation text at `0x8016F0C0` and the rectangle pointer. It then
loads the active driver record through `0x80178F40` and performs:

```c
result = driver->method_at_08(driver->context_at_0C,
                              rectangle,
                              8,
                              packed_color);
```

The indirect result remains in `v0` through the epilogue. Thus all framebuffer
mutation, synchronization, validation failure, and hardware effects belong to
the active backend; this public wrapper owns argument normalization and
dispatch.

## Complete caller census

An exhaustive aligned executable scan finds exactly six entry calls:

```text
800EE864 800EE8A8 800EE94C 800EE968 800EE98C 800EE9A8
```

All callers prepare a rectangle and RGB clear color in the surrounding display
setup code. There are no calls or direct jumps to any interior address, no
generated split, and the adjacent wrappers at `0x8015D454` and later dispatch
different GPU operations.

## Sources checked

- `disc/SCES_028.45`, excluding its `0x800` PS-X EXE header, for exact MIPS,
  packing, indirect ABI, caller census, and interior-target scan;
- `generated/SCES_028.45_full.ranges` for the exact owner boundary;
- surrounding canonical GPU-wrapper structure and retained diagnostic text for
  the `ClearImage` identity.
