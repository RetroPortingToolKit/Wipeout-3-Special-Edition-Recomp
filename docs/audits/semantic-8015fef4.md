# Semantic audit: `0x8015FEF4`

## Result

`0x8015FEF4` is the standard Psy-Q/BIOS `memset` thunk, cataloged as `Memset`
to match the symbol style used for other imported library routines. It selects
service `0x2B` at the A0 BIOS vector.

## Boundary and ABI

The generated manifest assigns exact owner range `0x8015FEF4..0x8015FF03`
(0x10 bytes); the next independent owner begins at `0x8015FF04`. The executable
thunk is the first 0x0C bytes:

```text
8015FEF4  addiu t2, zero, 0xA0
8015FEF8  jr    t2
8015FEFC  addiu t1, zero, 0x2B   # delay slot
```

`0x8015FF00` is an unreachable padding NOP after the unconditional jump. The
generator exposes it only as an artificial continuation/resume label; no guest
branch or call targets it.

The canonical ABI is:

```c
void *Memset(void *destination, int value, size_t count);
```

The thunk preserves `$a0/$a1/$a2`, supplying only the vector and selector. The
BIOS fills `count` bytes beginning at `destination` with the low eight bits of
`value` and returns the original destination pointer in `$v0`. A zero count
performs no writes.

## Effects and graphics use

The A0:2B identity is independently recorded in the repository's BIOS seed
provenance as the ROM `memset` service. The wrapper itself has no stack, RAM,
MMIO, GPU, GTE, or COP0 operation; the delegated BIOS routine performs the byte
writes. Several callers use it to zero small graphics or scene structures, but
the routine is the general C memory-fill primitive rather than a GPU command.

## Executable caller census

There are exactly twelve generated instances and twelve unique executable
`jal` sites, so there is no overlay duplication:

`0x800FF6EC`, `0x801012F0`, `0x8010285C`, `0x80102D84`, `0x8013102C`,
`0x8013104C`, `0x8013131C`, `0x8013132C`, `0x80133B08`, `0x801340D8`,
`0x8014004C`, and `0x8014005C`.

All target the true entry `0x8015FEF4`. No executable reference targets the
padding NOP at `0x8015FF00`, and there are no data-derived false callers.
