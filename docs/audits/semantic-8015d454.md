# Semantic audit: `0x8015D454`

## Result

`0x8015D454` is the PsyQ low-level VRAM upload entry `LoadImage2`.  It runs the
shared RECT diagnostic/validator and dispatches operation 8 to the active GPU
driver's image-transfer method.

## Boundary, ABI, and return

The function allocates a 32-byte frame at `0x8015D454`, saves `s0`, `s1`, and
`ra`, and preserves both incoming arguments across validation.  `jr ra` is at
`0x8015D4AC`; its delay slot restores `sp` at `0x8015D4B0`.  The next owner
begins at `0x8015D4B4`, proving `[0x8015D454,0x8015D4B4)`, `0x60` bytes.

The recovered ABI is:

```c
int LoadImage2(const RECT *rect, const uint32_t *source_words);
```

The active driver's return value is returned unchanged.  This owner does not
normalize it or wait for transfer completion; the higher retained `LoadImage`
wrapper provides queuing/critical-section handling and waits when required.

## Validation and driver call

First, the routine calls the shared owner at `0x8015D2A8` with:

```c
ValidateGpuRect("LoadImage", rect);
```

The NUL-terminated operation name is retained at `0x8016F0D8`.  The validator
owns malformed-RECT diagnostics; its scalar result is deliberately ignored by
this wrapper.

Next, the active GPU driver object is loaded from `0x80178F40`.  The routine
loads a context/port value from driver offset `+0x20`, loads the indirect
transfer method from offset `+0x08`, and invokes it as:

```c
driver->transfer(driver->context_20, rect, 8, source_words);
```

The fixed operation value 8 selects host-to-VRAM image upload in the shared
transfer method.  Neither driver nor method is null-checked; installed GPU
driver state is a library invariant.

`LoadImage2` itself performs no global store and accesses no GPU MMIO register.
GP0 image-load commands, DMA setup, and transfer progress are delegated to the
selected driver's method.  Its observable effects are the validator's optional
diagnostic and the delegated upload.

## Exhaustive callers and interior targets

An aligned scan of the retained EXE finds exactly five direct calls:

```text
800EE0C8 800EE1C4 800EED0C 8011626C 801182D8
```

They belong respectively to owners `0x800EDFC8`, `0x800EE11C`, confirmed
`LoadImage` (`0x800EEC80`), `RunRaceSession` (`0x80116128`), and `0x8011805C`.
This mix of direct library/system callers and the queued high-level wrapper is
consistent with a low-level transfer entry.

No external J or JAL targets an interior instruction.  The validator at
`0x8015D2A8` and the next owner at `0x8015D4B4` are independent callables.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS,
  operation-name bytes, and exhaustive caller/interior scans;
- `generated/SCES_028.45_full.ranges` and the generated owner;
- `docs/audits/semantic-800eec80.md` for the independently proven high-level
  `LoadImage` queue/wait contract and its direct call to this primitive;
- `docs/audits/semantic-8015d240.md` for the adjacent active-driver dispatch
  conventions and synchronization boundary.
