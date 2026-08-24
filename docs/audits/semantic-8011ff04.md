# Semantic audit: `0x8011FF04` / `ResetParticleTilePacketPool`

## Result and ABI

`0x8011FF04` is the allocation-count reset for the recovered particle tile
packet pool:

```c
void ResetParticleTilePacketPool(void);
```

It takes no arguments and stores zero to word `0x80175D40` in the `jr ra`
delay slot. There is no semantic return value.

The pool identity is proven by adjacent recovered code. Confirmed
`InitializeParticleTilePacketPool` initializes 32 records of 36 bytes at
`0x801D4EDC`; each contains three 12-byte GPU `TILE_1` packets, for 96 packets
total, and calls this leaf after initialization. The neighboring allocation
owner uses `0x80175D40` as its `count < 32` gate and indexes the same 36-byte
records. Thus this is a pool reset, not a generic global clear.

## Boundary and effects

The owner is exactly `0x8011FF04..0x8011FF0F` (0x0C bytes, three
instructions). The independent framed owner begins at `0x8011FF10`. There is
no padding, split, call, branch, or external interior target.

The count word is the only mutated state. The record payload and packet tags
are not touched; there is no allocation, GPU submission, MMIO, callback, or
synchronization effect.

## Callers

Three unique JAL sites target the true entry:

- `0x8011FEEC`, within `InitializeParticleTilePacketPool`;
- `0x80123470`;
- `0x80123ED4`.

There are no overlay duplicates or interior targets.
