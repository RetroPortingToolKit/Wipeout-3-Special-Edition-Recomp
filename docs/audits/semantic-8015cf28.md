# Semantic audit: `0x8015CF28` / `Strncmp`

## Result and ABI

`0x8015CF28` is the canonical PlayStation BIOS A0:18 `strncmp` trampoline:

```c
int Strncmp(const char *left, const char *right, size_t count);
```

It loads `t2 = 0xA0`, jumps to the A0 BIOS vector, and loads `t1 = 0x18` in
the jump delay slot. Incoming `a0`, `a1`, and `a2` and the caller's `ra` are
untouched, so the BIOS receives the two string pointers and byte bound directly
and returns its signed comparison result straight to the original caller.

## Boundary and effects

The generated owner is exactly `0x8015CF28..0x8015CF37` (0x10 bytes). The first
three words are executable; `0x8015CF34` is an unreachable padding NOP after
the vector jump. The independent BIOS trampoline at `0x8015CF38` is not part
of this owner. No direct target enters the padding word.

The stub itself has no memory, global, stack, hardware, or callback effects.
String reads, stopping at `count` or a terminating null, fault behavior, and
the exact negative/zero/positive magnitude are BIOS-owned. The ABI guarantee
needed by callers is the signed three-way comparison result, not a normalized
`-1/0/1` value.

## Callers

There are seven unique direct JAL sites:

- `0x800E5E28` (present in two generated overlay variants);
- `0x80131C34` and `0x80131E1C`;
- `0x801408AC` and `0x80140970`;
- `0x80164118` and `0x801641AC`.

Thus the generated call count is eight while the executable has seven unique
call addresses. Every call targets `0x8015CF28`; none targets the padding or an
interior instruction. The call families use the result as a comparison test,
consistent with the canonical BIOS service identity.
