# Semantic audit: `0x80111C0C` / `SwapType4Or8QuadUvPairs`

## Result

`0x80111C0C` is an in-place, type-gated quad UV reorder now named
`SwapType4Or8QuadUvPairs`:

```c
void SwapType4Or8QuadUvPairs(uint8_t **record_pointer);
```

For pointed records whose first byte is 4 or 8, it swaps UV pair 0 with pair 1
and pair 2 with pair 3. Every other record type returns unchanged.

## True owner and generated split

Generated metadata reports a `0x2C`-byte entry range at `0x80111C0C` and a
separate `0x80`-byte range at `0x80111C38`. They are one logical owner:

```text
80111C0C  move  a1,a0
80111C10  lw    v1,0(a1)
80111C18  lbu   a0,0(v1)
80111C1C  addiu v0,zero,4
80111C20  beq   a0,v0,0x80111C38
80111C24  addiu v0,zero,8
80111C28  beq   a0,v0,0x80111C38
80111C2C  nop
80111C30  jr    ra
80111C34  nop
```

Both accepted-type branches target `0x80111C38`, which immediately uses the
live `a1/v1` state established by the entry. The continuation has no independent
argument setup or prologue. It returns through `0x80111CB0..0x80111CB4`, and
the next callable begins with its own global load/frame at `0x80111CB8`.

The complete function is therefore `[0x80111C0C,0x80111CB8)`, exactly `0xAC`
bytes. The split is a generated control-flow artifact, not two callables.

## ABI, exact permutation, and return

`a0` points to a word containing the record pointer; the extra indirection is
real and is used repeatedly throughout the continuation. The implementation
does not accept a null outer or inner pointer. No scalar result is established,
and all callers ignore register residue, giving it a void ABI.

For type 4 or 8, old bytes are saved before overlapping stores and the exact
permutation is:

```text
new[+08] = old[+0A]    new[+0A] = old[+08]
new[+09] = old[+0B]    new[+0B] = old[+09]
new[+0C] = old[+0E]    new[+0E] = old[+0C]
new[+0D] = old[+0F]    new[+0F] = old[+0D]
```

Treating each adjacent byte pair as `(u,v)`, this is:

```c
swap(record->uv[0], record->uv[1]);
swap(record->uv[2], record->uv[3]);
```

There is no byte reversal inside a pair. Bytes `+0..+7`, all bytes from `+10`
onward, the outer pointer, globals, and allocator state remain unchanged. Type
checks are exact equality checks; no bitmask or type range is accepted.

## UV and conversion-context evidence

The eight moved bytes form four consecutive two-byte coordinate pairs, and the
operation preserves each pair while changing quad corner order. Callers invoke
it in asset/primitive conversion paths alongside:

- `SwapRgb24RedBlue` for embedded three-byte colors;
- `ByteSwapTwoU16Values` for actual adjacent 16-bit numeric fields;
- larger record reorders that move complete three-byte color groups.

Because this owner does not byte-swap the moved pairs, interpreting them as
four byte-sized `(u,v)` values is stronger than interpreting them as four
16-bit indices. The accepted type codes and pairwise corner reorder identify
the affected records as two quad variants. The name remains explicit about the
observed type values rather than guessing their higher-level enum spellings.

## Complete callers and interior targets

An exhaustive aligned scan of the retained executable finds four unique JAL
instructions:

```text
800E6394 800E66A0 80110C98 80110D04
```

All four target the true entry at `0x80111C0C`; none targets the continuation
at `0x80111C38` or another interior address. The entry's two conditional
branches are the only transfers into the continuation.

The generated corpus emits eight call occurrences because the conversion
paths are represented in mirrored/overlapping translations. Those emissions
resolve to the four unique executable instruction words above and do not
increase the semantic caller count.

At every site the caller passes the address of a stack word holding the record
pointer. Two sites call only the byte-order helper before this reorder; two
also run a later, larger field permutation. This explains both the outer
pointer ABI and the converter's in-place role.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, unique callers, arguments, and target scans;
- `generated/SCES_028.45_full.ranges` for the generated `0x2C + 0x80` split;
- generated call sites for the eight emitted versus four unique distinction;
- `docs/audits/semantic-80112048.md` and
  `docs/audits/semantic-80112064.md` for adjacent conversion operations and
  their proven field-width semantics.
