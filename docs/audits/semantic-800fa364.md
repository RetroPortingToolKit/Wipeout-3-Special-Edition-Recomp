# Semantic audit: `0x800FA364` / `ApplySceneObjectGroupProperty`

## Result and ABI

`0x800FA364` scans the active 0xCC-byte scene-object records, selects every
record matching a caller key through either of two identity fields, and applies
one of four property updates.  A conservative behavior-level name and ABI are:

```c
void ApplySceneObjectGroupProperty(
    int32_t key,
    uint8_t selector,
    int32_t value);
```

Only the low byte of `a1` selects behavior.  `a0` and `a2` remain full-width
for comparisons/arithmetic, although all observed keys originate as unsigned
bytes and byte destinations truncate `value`.  No caller consumes `v0`, and
the exit leaves loop intermediates, so there is no semantic return.

## Exact owner and target census

The generated table assigns `[0x800FA364, 0x800FA490)`, exactly `0x12C` bytes,
to this owner.  The first two instructions load the record count before the
48-byte frame is opened; they belong to the function.  The common epilogue
restores `ra` and `s0..s6`, returns at `0x800FA488`, and restores `sp` in the
delay slot at `0x800FA48C`.  Independent owners end/start exactly at
`0x800FA364` and `0x800FA490`; there is no prelude or tail split.

An aligned scan of the authenticated executable finds exactly eight entry
JALs:

```text
800F9E88 800F9E9C 800F9EC4
800FA658 800FA670 800FA6A8 800FA6BC
800FB9B8
```

There are no direct JALs to any interior instruction in
`[0x800FA368, 0x800FA490)`.  The only outgoing call is `NextRandom15` at
`0x800FA420`, reached solely by selector 1 on a matching record.

## Record scan and matching

The current record count is word `0x80187CE8`; the base pointer is word
`0x8018811C`.  Records have exact stride `0xCC`.  The function loops from index
zero while `index < count`, rereading both the base and count during the loop.
Nonpositive initial count exits without reading the base.

A record matches when either condition is true:

```c
(int32_t)*(int16_t *)(record + 0x08) == key
|| (uint32_t)*(uint8_t *)(record + 0x01) == (uint32_t)key
```

The first field is sign-extended; the second is zero-extended.  The comparison
does not truncate `key`.  Both are OR alternatives, and every matching record
is updated—the scan never stops after the first match.  This group-wide
behavior, common byte-1 keys at all executable callers, and the scene-object
record consumers support “group property” while avoiding an unsupported
source-level class/tag name.

## Selector behavior

For each match, `selector = a1 & 0xFF` performs:

| Selector | Exact update |
|---:|---|
| 0 | load pointer at record `+0xBC`; store low byte of `value` at pointee `+4` |
| 1 | call `NextRandom15`; store the low 16 bits of `(random & value) + (value >> 1)` at record `+0xA2` |
| 2 | store low byte of `value` at record `+2` |
| 3 | store low byte of `value` at record `+3` |

Every other selector performs no update but still scans all records.  Selector
0 does not validate the linked pointer.  Selector 1 uses arithmetic right shift
for `value >> 1`, does not require a contiguous low-bit mask, and truncates the
sum to 16 bits.  Observed selector-1 values are `5` and `31`; other callers use
selectors 0, 2, and 3 with small state bytes.

## Caller and state context

The first three calls are in a scene-object spatial/state pass and use a
record's byte-1 group key to toggle linked state or record byte `+2/+3`.  Four
calls in owner `0x800FA490` use the same key to clear/set linked state or seed
the randomized `+0xA2` field.  The final call at `0x800FB9B8` sets byte `+2`
for a group during an object state-machine transition.

The function allocates nothing, emits no GPU packet, and changes no record
count/base pointer.  Its persistent effects are confined to all matched scene
records (or their linked `+0xBC` objects) plus RNG-state advancement once per
match for selector 1.  Consequently multiple matches consume multiple random
values and receive independently randomized halfwords.

## Confidence

`confirmed`: exact boundary including pre-frame instructions, ABI, exhaustive
entry/interior-target census, record base/count/stride, dual match predicates,
all-match scan, four selector writes, RNG formula/truncation, unsupported-mode
no-op, and caller state patterns are instruction-proven.  The chosen name is
deliberately structural; original field terminology is not claimed.
