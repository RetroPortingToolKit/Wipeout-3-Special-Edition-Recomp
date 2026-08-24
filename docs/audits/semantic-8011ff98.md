# Semantic audit: `0x8011FF98` / `DrainParticleEffectQueue`

## Result and exact boundary

`0x8011FF98` is `DrainParticleEffectQueue`, the queue consumer adjacent to the
two recovered particle producers. The generated range table proves
`[0x8011FF98, 0x801202F0)`, exactly `0x358` bytes and 44 blocks. The owner ends
with its register restore, `jr ra` at `0x801202E8`, and 64-byte frame release
in the delay slot at `0x801202EC`; the next independent owner starts at
`0x801202F0`.

## ABI and behavior

The routine reads no incoming argument register and establishes no stable
scalar result:

```c
void DrainParticleEffectQueue(void);
```

It first tests the reentrancy flag at `0x80175DE4`. On a clear flag it sets the
flag, passes through `SimVblankGate`, and advances the particle time/cursor
word at `0x801D59E8` by 20. It compares the producer and consumer cursors at
`0x80175D54/0x80175D58`; unequal cursors advance the consumer, while equality
selects the next 80-byte destination slot from the 16-entry ring rooted at
`0x801D54D8` and advances the paired masked ring cursors at
`0x80175D48/0x80175D4C`.

The selected destination contains two 40-byte halves. For each of the two
pending 40-byte records rooted at `0x801D5438`, the drain examines its active
byte and `0x80` sentinel/state byte, applies the private helpers at
`0x80165324` and `0x801654C4` where required, updates the associated state
words rooted at `0x801D5414`, and copies all ten words into the corresponding
destination half. Consumed source records are marked with `0xFF`; inactive
state entries are cleared or activated according to the helper result. After
both halves it invokes the optional callback stored at `0x80175D44`, then
always clears the reentrancy flag before restoring registers.

This establishes a queue drain and ring publication role. The narrower name
does not claim a particular visual subtype: `SpawnType2ParticleEffect` and
`SpawnRacerType1ParticleBurst` are producers, while this owner batches their
downstream effect records and publishes a pair for the callback/render path.

## Caller and target proof

There are no encoded direct `jal` calls and no absolute code pointers to the
entry in the executable; the generated root is therefore reached through the
runtime particle/update registration path rather than a static direct call.
An exhaustive executable scan also finds no call or jump to any address inside
`(0x8011FF98, 0x801202F0)`. The many continuation labels in dispatch metadata
are internal branches and call returns belonging to this single owner, not
independent entries. Its direct callees are `SimVblankGate`, private helpers
`0x80165324` and `0x801654C4`, plus the optional indirect callback from
`0x80175D44`.
