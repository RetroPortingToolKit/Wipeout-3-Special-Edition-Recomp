# Semantic audit: `0x800EEC80` / `LoadImage`

## Result and ABI

`0x800EEC80` is the title's retained Psy-Q `LoadImage` wrapper. Every recovered
TIM loader passes it a PlayStation `RECT *` plus a source-word pointer, and its
callee/queue behavior matches the SDK's serialized CPU-to-VRAM transfer path.

```c
void LoadImage(const RECT *rect, const uint32_t *source_words);
```

`a0` and `a1` are preserved across synchronization and used unchanged as the
transfer rectangle and source. Callers do not consume a semantic result; the
final `v0` is incidental state left by the synchronization helpers.

## Exact owner and callers

The function spans `[0x800EEC80, 0x800EED58)`, exactly `0xD8` bytes, with ten
basic blocks. The next independent callable begins at `0x800EED58`.

An aligned executable scan finds exactly sixteen direct calls to the entry and
none to an interior instruction:

```text
800F4CB8  800F4D00  800F4D48  800F4D90
80111268  80111308  80111420  8011159C  80111664
801118F0  80111A94  80111AC8
80141B88  80141D10  80141D3C  80141E3C
```

The `0x80111xxx` and `0x80141xxx` groups lie in the two independently recovered
TIM texture-loading implementations, providing cross-overlay semantic proof.

## Serialized transfer behavior

The wrapper first calls `EnterNestedInterruptCriticalSection`. It sets its
stack-local completion flag to one, then compares the transfer ring's producer
and consumer indices at `0x8017F11C/0x8017F120`.

When the ring is empty and no transfer is active, it marks the shared active
state and calls the low-level GPU image transfer at `0x8015D454` directly with
the original `(rect, source_words)` pair.

Otherwise it appends a 12-byte request to the eight-entry ring rooted at
`0x8017F128`:

```text
+0  signed halfword -2 (image-upload command)
+4  RECT pointer
+8  source-word pointer
```

The producer index advances modulo eight. The request also records a pointer
to the caller's completion flag, allowing the interrupt-driven consumer to
clear it when the queued upload has completed.

After `LeaveNestedInterruptCriticalSection`, the queued path repeatedly:

1. calls the synchronization primitive at `0x8015D240` with argument zero;
2. briefly re-enters the nested critical section;
3. reads the completion flag;
4. leaves the critical section and repeats while it remains one.

The immediate path skips this wait because submission is already owned by the
active GPU transfer machinery. Both paths balance their critical-section entry
and exit calls.

## Confidence

`confirmed`: exact range, ABI, all callers, absence of interior entries, ring
layout and modulo, immediate-versus-queued control flow, completion wait, and
use by both TIM loaders are instruction-proven. The canonical Psy-Q name is
also independently established by those texture semantics.
