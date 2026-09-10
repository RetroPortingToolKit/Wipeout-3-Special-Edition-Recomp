# Semantic audit: `0x8015B3CC`

## Result

`0x8015B3CC` is the confirmed racer-effect spawn adapter
`DispatchRacerEffectSpawn`. It forwards ordinary effect kinds to
`SpawnRacerEffectRecords` and expands composite kind 13 into two consecutive
spawn requests, kinds 14 and 15.

Its recovered ABI is:

```c
void DispatchRacerEffectSpawn(
    void *effect_owner,
    void *effect_record_pool,
    int32_t racer_or_value,
    uint8_t effect_kind);
```

The first three arguments retain the meanings proven for the callee at
`0x8014EA88`: owner, base of thirty 0x60-byte effect records, and the
racer/value argument used by the selected kind. The wrapper explicitly
truncates the fourth argument to eight bits. It exposes no stable semantic
return value.

## Exact owner and interior targets

The generated range is exactly `[0x8015B3CC,0x8015B444)`, `0x78` bytes. The
entry allocates a 32-byte frame and saves `s0..s2` plus `ra`. All paths join at
the epilogue beginning `0x8015B42C`, return at `0x8015B43C`, and restore the
frame in the delay slot at `0x8015B440`. The next generated owner starts at
`0x8015B444`; its independent `jr ra; nop` is not part of this function.

Dispatch rows at `0x8015B3F8`, `0x8015B400`, `0x8015B414`, `0x8015B41C`, and
`0x8015B42C` are post-call or branch continuations retaining the saved frame.
No external JAL or jump targets any interior address.

## Behavior

For kinds other than 13, the wrapper restores the saved first three arguments
and calls:

```c
SpawnRacerEffectRecords(owner, pool, racer_or_value, effect_kind);
```

For kind 13, it instead makes two calls with identical first three arguments:

```c
SpawnRacerEffectRecords(owner, pool, racer_or_value, 14);
SpawnRacerEffectRecords(owner, pool, racer_or_value, 15);
```

This explains all three direct call sites to `0x8014EA88` at `0x8015B3F8`,
`0x8015B40C`, and `0x8015B424`. Pool exhaustion or kind-specific spawn failure
is handled only by the callee; the adapter does not retry, allocate, or inspect
a result. Its effects are exactly the callee's effect-record/owner/audio state
effects for one or two spawn attempts.

## Caller census and overlay duplication

There are six unique direct entry callers:

```text
overlay code:
  0x800E33D4
  0x800E3404
  0x800E3470
  0x800E34E8
  0x800E3538

main image:
  0x8011C43C
```

The five `0x800E...` callers belong to the loaded overlay region, while the
`0x8011C43C` caller is resident main-image code. The generated corpus contains
one instance of each address: overlay placement does not create additional
logical callers here. Every caller passes a kind byte from owner offset
`+0x15F` (or the equivalent live owner record) and targets the true entry.

Thus the exhaustive count is six unique executable callers and zero interior
callers. The symbol is an adapter name rather than another name for the callee:
this owner contributes the distinct kind-13-to-14/15 composite dispatch rule.
