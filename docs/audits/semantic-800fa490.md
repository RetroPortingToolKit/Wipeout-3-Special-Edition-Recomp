# Semantic audit: `0x800FA490`

## Result and ABI

`0x800FA490` is the confirmed relation-driven scene-object pass
`UpdateRelatedSceneObjectGroups`:

```c
void UpdateRelatedSceneObjectGroups(const RelationContext *context);
```

`a0` is retained as `context`; its word at `+8` is the anchor used by both
bounded pointer-chain tests. No other argument register is consumed. The
routine creates a 48-byte frame, preserves `s0..s5` and `ra`, and establishes
no semantic scalar return.

The name is deliberately structural. The evidence proves scene-object records,
group updates, and bounded relation tests, but not the original gameplay term
for the context or its links.

## Exact owner and interior targets

The generated range is exactly `[0x800FA490,0x800FA6FC)`, `0x26C` bytes. The
initial global load at `0x800FA490` precedes the frame allocation and belongs to
the function. The common epilogue begins at `0x800FA6D8`, returns at
`0x800FA6F4`, and restores the frame in the delay slot at `0x800FA6F8`. The
next independent owner starts at `0x800FA6FC`.

All 29 additional dispatch rows inside the range are ordinary branch-loop or
nested-call continuations. An exhaustive generated-source search finds no JAL
or jump to an interior row. The only outgoing callable owner is the already
audited `ApplySceneObjectGroupProperty` at `0x800FA364`, invoked four times.

## Gates, record scan, and eligibility

The pass exits before scanning if word `0x801883A4` is positive, or when that
word is nonpositive and word `0x80188118` is also nonpositive. Otherwise it
scans the `0xCC`-byte records based at pointer word `0x8018811C`, bounded by
signed count word `0x80187CE8`. A nonpositive count exits.

For each record, processing continues only when all instruction-proven filters
pass:

- unsigned byte `+0` is less than two;
- signed halfword `+8` equals `-1`;
- byte `+5` equals one;
- byte `+3` is nonzero;
- pointer `+0xBC` is dereferenced and its byte `+4` is not five.

The routine assumes the accepted record's `+0xBC` pointer is valid. It does not
allocate, alter the array base/count, or write records directly.

## Relation tests and effects

The context anchor is `context->word_08`. The candidate record contributes its
word at `+0x18`. The function accepts the candidate when either bounded search
connects those pointers:

1. compare the anchor with candidate `+0x18`, then follow up to six `+0x0C`
   links looking for equality;
2. starting from the anchor, follow up to five `+0x08` links looking for the
   candidate `+0x18` pointer.

Failure of both searches skips the candidate. A successful relation causes
group-property calls keyed by scene-record byte `+1`. The four static call
sites use selector/value pairs `(0,5)`, `(1,31)`, `(0,0)`, and `(0,1)`;
control flow and linked metadata choose which subset executes. Through the
audited callee, selector zero writes the value to byte `+4` of every matching
record's linked object, while selector one seeds matching records' `+0xA2`
halfword from `NextRandom15` with mask 31. Thus persistent effects are group
state changes and possible RNG advancement, not changes to the relation links.

## Exhaustive caller and overlay census

Six generated call instances reduce to five unique guest instructions:

```text
800F9F7C
801509A4
80155DF0
801598E8  (emitted in two overlay images)
8015B004
```

Every call targets the owner entry and passes a context pointer in `a0`. The
duplicate at `0x801598E8` is byte-identical guest code represented in
`SCES_028.45_full_49.c` and `SCES_028.45_full_50.c`; it is one executable call
site, not two logical callers. No call targets an interior continuation.

## Confidence

`confirmed`: boundary, ABI, global gates, record base/count/stride, eligibility
filters, bounded bidirectional relation test, callee selectors/values, unique
caller census, overlay duplication, and absence of interior targets are all
instruction-backed. Source-level names for the context and individual fields
remain intentionally unspecified.
