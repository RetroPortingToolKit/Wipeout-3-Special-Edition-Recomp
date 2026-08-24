# Semantic audit: `0x8011FE94`

## Result and boundary

`0x8011FE94` is the packet-pool initializer now named
`InitializeParticleTilePacketPool`. Its exact generated range is
`R 8011FE94 70`, or `[0x8011FE94, 0x8011FF04)`. It creates a 24-byte frame,
saves `ra`, and returns through the sole epilogue at `0x8011FEF4..0x8011FF00`.
The adjacent randomized-particle adapter ends at `0x8011FE93`; `0x8011FF04`
begins an independent 12-byte count-reset leaf.

An aligned scan of every direct jump/call in the retained EXE found no target
inside the owner. An exact-body scan found only `0x8011FE94`, ruling out an
overlay duplicate or generated split.

## ABI and exact effect

The function consumes no incoming registers. Its only caller does not use
`v0`, and its callee leaves only an incidental address-building value there,
so the semantic ABI is:

```c
void InitializeParticleTilePacketPool(void);
```

The storage at `0x801D4EDC` is 32 records of 36 bytes. For each record the
routine visits three consecutive 12-byte packet slots and writes exactly two
bytes per slot:

```text
packet +3 = 2
packet +7 = 0x68
```

In the standard PSX primitive layout, byte `+3` is the packet-tag word count
and byte `+7` is the GPU command byte. A two-word payload with GP0 opcode
`0x68` is a `TILE_1` (one-pixel tile) packet. Therefore each 36-byte record is
preconfigured as three TILE_1 packets, for 96 packets total. The double loop is
exactly 32 outer iterations and three inner iterations; it neither clears nor
initializes packet tag links, RGB bytes, or XY halfwords.

After all packets are configured, the routine calls the adjacent leaf at
`0x8011FF04`, which stores zero to the signed/word allocation count at
`0x80175D40`. The neighboring packet producer at `0x8011FD1C` proves the pool
relationship: it refuses allocation once that count reaches 32, indexes the
same base with 36-byte records, fills each record's three packet colors and
coordinates, links them into an ordering table, and increments the count.

## Call graph

There is exactly one direct entry call in the retained EXE, at `0x80133B8C`.
It occurs in a broader initialization owner and immediately continues into
other global setup. No argument is prepared for this call and no return value
is consumed.

The only outgoing call is `0x8011FF04`, the three-instruction allocation-count
reset described above. No call or jump enters an interior block.

## Sources checked

- `disc/SCES_028.45`, after the 0x800-byte PS-X EXE header, for exhaustive
  callers, interior targets, and body duplication;
- `generated/SCES_028.45_full_32.c` and `.ranges` for the exact loops, writes,
  boundary, callee, and neighboring packet producer;
- `docs/audits/semantic-8011fe6c.md` for the boundary and semantics of the
  adjacent, independent randomized-particle adapter.
