# Semantic audit: `0x8016B264` / `DispatchMdecDmaCommand`

## Result

`0x8016B264` and the generated owner at `0x8016B274` are one logical varargs
function, now named `DispatchMdecDmaCommand`:

```c
int DispatchMdecDmaCommand(int selector, ...);
```

Selectors 0 through 3 synchronize and configure the MDEC DMA path. The final
selector consumes `(address, size)` and starts the currently selected MDEC DMA
channel.

## Boundary and varargs proof

The generated range table incorrectly separates a `0x10`-byte owner at
`0x8016B264` from a `0x270`-byte owner at `0x8016B274`. The first four
instructions are:

```text
8016B264  sw a0,0(sp)
8016B268  sw a1,4(sp)
8016B26C  sw a2,8(sp)
8016B270  sw a3,12(sp)
```

The continuation then subtracts `0x18` from `sp` and sets its vararg cursor to
new `sp + 0x1C`, exactly original `sp + 4`, the spilled `a1` slot. Selector 2
loads that word directly. Selector 3 advances the cursor and reads the saved
`a1/a2` as its address and size. Entering at `0x8016B274` would leave these
arguments unmaterialized and violate the body contract.

All six external calls target `0x8016B264`; none targets `0x8016B274` or any
other interior address. All paths restore through `0x8016B4D4..0x8016B4E0`,
and the next independent owner begins at `0x8016B4E4`. The exact logical owner
is therefore `[0x8016B264,0x8016B4E4)`, `0x280` bytes.

## Selector ABI and behavior

Unknown selectors return zero without changing hardware. Valid selectors are:

| Selector | Arguments | Exact behavior |
| ---: | --- | --- |
| 0 | none | Set the direction-state word to 1, wait for the MDEC count/status halfword at `+0x1A6` to equal its cached value, then OR control bits `0x30` into halfword `+0x1AA`. |
| 1 | none | Clear the direction-state word, perform the same bounded count wait, then replace control bits `0x30` with `0x20`. |
| 2 | one word | Shift the argument right by the configured count shift, cache the resulting halfword, and write it to MDEC state `+0x1A6`. |
| 3 | address, size | Wait until control bits `+0x1AA & 0x30` match `0x30` for direction state 1 or `0x20` otherwise, configure the matching DMA direction, and launch the transfer. |

Each hardware wait permits at most `0xF00` unsuccessful observations; timeout
returns `-2`. Successful operations and unsupported selectors return zero.

For selector 3, `address` is saved as the DMA memory address. The size is
converted to a block count by `size >> 6` plus one when any low six bits are
set, exactly `ceil(size / 64)`. The DMA block-control word is
`(ceil(size/64) << 16) | 0x10`. Channel control is `0x01000200` for direction
state 1 and `0x01000201` otherwise. The preceding helpers update the shared
DMA priority/control register for the corresponding MDEC channel.

These paired DMA directions, the 64-unit block accounting, the surrounding
decoder output wrappers, and direct use by the MDEC callback establish the
MDEC identity without assigning speculative public PsyQ API status to this
internal selector interface.

## Exhaustive callers and interior targets

The retained executable contains six unique JALs to the true entry:

```text
80166580  80166588  8016659C  8016B51C  8016B524  8016B534
```

The first triplet belongs to the MDEC callback/chunk-feed path; the second is
inside the decoder output wrapper beginning at `0x8016B4E4`. Both sequences
issue selectors 2, 1, and 3 in that order. Selector 2 receives the transfer
count, selector 1 selects the output-side DMA state, and selector 3 receives
the destination address and chunk size.

Generated output also contains exactly six call emissions, so there is no
overlay-call duplication. No external J/JAL targets the generated continuation
or another interior block. Direct jumps within the owner only join its common
return or selector-3 setup; the two internal JALs at `0x8016B430` and
`0x8016B440` target the adjacent DMA-control helpers outside this owner.

## Sources checked

- `disc/SCES_028.45`, accounting for its `0x800`-byte PS-X EXE header, for
  exact instructions, vararg stack provenance, callers, and target scans;
- `generated/SCES_028.45_full.ranges` for the false `0x10 + 0x270` split;
- generated call sites for the six-emission count and selector sequences;
- adjacent decoder output and DMA-control helpers for the MDEC channel context.
