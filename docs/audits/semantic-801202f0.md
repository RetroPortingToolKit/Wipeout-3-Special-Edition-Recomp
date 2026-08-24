# Semantic audit: `0x801202F0` / `LatchParticleEffectBatch`

## Result and boundary

`0x801202F0` is `LatchParticleEffectBatch`, the consumer immediately following
`DrainParticleEffectQueue`. The generated range table proves
`[0x801202F0, 0x801203F0)`, exactly `0x100` bytes and seven blocks. Its final
return is at `0x801203E8`, with the 24-byte frame release in the delay slot at
`0x801203EC`; the next independent owner starts at `0x801203F0`.

## ABI and behavior

The owner reads no incoming argument and returns a Boolean publication result:

```c
int LatchParticleEffectBatch(void); // 1 copied, 0 no new batch
```

It enters `EnterNestedInterruptCriticalSection`, initializes its result to
zero, and compares the consumed cursor at `0x80175D50` with the published ring
cursor at `0x80175D48`. If they match, no batch is pending. If they differ, it:

1. selects `0x801D54D8 + published_cursor * 80`;
2. advances `0x80175D48` modulo 16;
3. copies exactly 20 words (80 bytes, two adjacent 40-byte effect records) to
   the working pair rooted at `0x801D5488`; and
4. changes the return value to 1.

Both paths call `ExitCriticalSection` before returning the saved result. The
routine does not interpret or mutate individual record fields. Relative to
`DrainParticleEffectQueue`, which constructs/publishes the ring slot and can
invoke the effect callback, this owner atomically latches one already-published
two-record batch into stable working storage for downstream consumers.

## Callers and interior targets

There are exactly three executable direct calls to the entry:

- `0x801167B0`, in owner `0x80116760`, before the adjacent downstream effect
  update call;
- `0x80122D3C`, in owner `0x8012268C`; and
- `0x801234A8`, in owner `0x80123250`.

The latter two frontend/race-loop sites consume the Boolean result after the
call; the first participates in the same batch/update cadence. An exhaustive
scan of executable jump and call encodings finds no target inside
`(0x801202F0, 0x801203F0)`, and there is no duplicate owner. The only direct
callees are the paired critical-section entry and exit functions.
