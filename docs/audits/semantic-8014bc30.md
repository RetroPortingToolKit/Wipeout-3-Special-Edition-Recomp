# Semantic audit: `0x8014BC30` / `GetCurrentArchiveEntryOutputSize`

## Result

`0x8014BC30` is the argument-free current archive-record size accessor now
named `GetCurrentArchiveEntryOutputSize`:

```c
uint32_t GetCurrentArchiveEntryOutputSize(void)
{
    int32_t index = gCurrentArchiveEntryIndex;
    if (index == -1)
        return 0;
    return gArchiveEntryRecords[index].output_size;
}
```

It uses the same record index/table as `AcquireCurrentArchiveEntry` and returns
the already-proven `+4` output-size field without acquiring or materializing
the resource.

## True boundary and generated split

Generated metadata reports a `0x1C`-byte owner at `0x8014BC30` followed by a
separate `0x20`-byte range at `0x8014BC4C`. They are one logical function:

```text
8014BC30  lui   v0,0x801F
8014BC34  lw    a0,-0x67F8(v0)  # 0x801E9808 current index
8014BC38  addiu v1,zero,-1
8014BC3C  bne   a0,v1,0x8014BC4C
8014BC40  lui   v0,0x801F
8014BC44  jr    ra
8014BC48  move  v0,zero

8014BC4C  sll   v1,a0,1
8014BC50  addu  v1,v1,a0
8014BC54  lw    a0,-0x6804(v0)  # 0x801E97FC table pointer
8014BC58  sll   v1,v1,2         # index * 12
8014BC5C  addu  v1,v1,a0
8014BC60  lw    v0,4(v1)
8014BC64  jr    ra
8014BC68  nop
```

The entry's taken branch reaches `0x8014BC4C` with the `0x801F0000` address
base established in its own delay slot. The continuation has no independent
entry setup. The next callable starts with a 0x30-byte frame at `0x8014BC6C`.
The true owner is therefore `[0x8014BC30,0x8014BC6C)`, exactly `0x3C` bytes.

## ABI, return, and effects

The function reads no incoming argument before overwriting its register, so
its ABI is `uint32_t(void)`. It returns exactly:

```text
0                                  if *(int32_t *)0x801E9808 == -1
*(uint32_t *)(table + index*12+4)  otherwise
```

No check handles other negative or out-of-range indices; surrounding archive
management is responsible for maintaining either `-1` or a valid index. The
routine performs two global loads plus the selected record load, calls no
callee, and writes no memory, allocator state, index, or hardware register.

## Record identity proof

The independently audited `AcquireCurrentArchiveEntry` establishes the shared
record layout:

```c
struct ArchiveEntryRecord {
    uint32_t archive_offset; // +0
    uint32_t output_size;    // +4
    uint32_t stored_size;    // +8
};
```

That accessor uses `+4` as allocation/copy size and compares it with `+8` to
choose direct-copy versus packed/decode paths. This leaf selects the same table
at `0x801E97FC`, same signed index at `0x801E9808`, same 12-byte stride, and
returns precisely `+4`; `output_size` is therefore proven rather than inferred
only from consumer arithmetic.

## Complete caller and interior-target census

An exhaustive aligned instruction-word scan of the retained executable finds
exactly eight direct calls:

```text
80110B0C 80117A40 8012CEF4 80141104
80141E9C 80144C74 80144D60 8014AD0C
```

All eight target `0x8014BC30`. No direct `j` or `jal` targets the generated
continuation at `0x8014BC4C` or any other interior address through
`0x8014BC68`, proving that the continuation is not separately callable.

Every caller invokes `AcquireCurrentArchiveEntry` at `0x8014B830` immediately
before this accessor. Consumers pair the returned data pointer with this size,
store or scale the size, allocate dependent storage, or check pointer-plus-size
range arithmetic. That pairing independently confirms the current-entry size
contract. All callers consume `v0`; none expects side effects.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, caller windows, and exhaustive aligned target scans;
- `generated/SCES_028.45_full.ranges` for the generated `0x1C + 0x20` split;
- `docs/audits/semantic-8014b830.md` for independently proven current-record
  selection, layout, and `output_size` semantics;
- `docs/audits/semantic-8014b96c.md` for current-index/table initialization and
  lifetime context.
