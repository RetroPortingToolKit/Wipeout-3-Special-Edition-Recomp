# Semantic audit: `0x80141F68`

## Result and boundary

`0x80141F68` is the argument-free renderer batch drain
`DrainQueuedRenderObjects` (`known`).  Its generated range is exactly
`R 80141F68 15C`: a 348-byte function at `0x80141F68..0x801420C3`.
It creates a 40-byte frame, saves `ra` and `s0..s4`, and returns through the
`jr ra` at `0x801420BC` with the stack restore at `0x801420C0`.  The generated
piece at `0x801420C4` is outside this owner and is not an internal entry.

The routine accepts no meaningful register argument.  All inputs are global
queue state, and no caller consumes a scalar return.

## Queue and queued-object layout

The signed word at `0x801762D0` is the queued-entry count.  A non-positive
count skips both loops.  For positive counts, the code proves three parallel
arrays:

```c
struct QueuedRenderBatch {
    int32_t count;             // 0x801762D0
    void *objects[];           // 0x801E2624, 4-byte stride
    uint8_t modes[];           // 0x801E248C, 1-byte stride
    void *mode1_aux[];         // 0x801E24CC, 4-byte stride
};

struct QueuedRenderObject {
    /* ... */
    void *render_state;        // +0x20
};
```

Only these offsets and widths are claimed.  In the first pass,
`objects[i]->render_state` is loaded through object offset `+0x20`; halfword
`render_state + 0x40` is set to one, and the state pointer is passed to
`0x80135FE0`.  Thus every queued state is activated/prepared before any mesh
submission.  The loop uses the count at `0x801762D0` and advances the object
array by four bytes per entry.

After the first pass, `0x8015F6A4` is called with the viewport/projection value
from `0x801E14F0`.  The second pass then walks the same object pointers in lock
step with the byte modes and auxiliary-word array.

## Wrapper selection and submission

The mode byte has three observed submitting values:

- mode `0`: call the tail-entry wrapper at `0x80171AF4` with
  `a0 = objects[i]`, `a1 = 0`; that wrapper sets `a2 = 0` and selector `v0 = 0`;
- mode `1`: call `0x80171AE4` with `a0 = objects[i]`, `a1 = 0`, and
  `a2 = mode1_aux[i]`; that wrapper derives its special odd `a2` value and sets
  selector `v0 = 5`;
- mode `2`: call `0x80171AD8` with `a0 = objects[i]`, `a1 = 0`; that wrapper
  sets `a2 = 0` and selector `v0 = 20`.

All three wrappers tail-enter the shared body
`TransformAndSubmitMeshPrimitives` at `0x80171AFC`.  Other unsigned mode-byte
values advance the parallel cursors without submitting.  This proves that the
routine drains a heterogeneous render queue; it is not itself the primitive
converter.

At `0x801420A8`, after both passes (or immediately after the projection setup
when the count is no longer positive), the routine stores zero to
`0x801762D0`.  The clear is unconditional on every normal return path, so the
batch cannot be replayed by a later call unless producers repopulate it.

## Callers

There are 23 direct `jal 0x80141F68` sites and no observed interior calls:

- `0x800EF278` (`0x800EF038`), `0x800F035C` (`0x800F0264`);
- `0x800FA9A4` (`0x800FA864`), `0x800FBFF8` (`0x800FBEB8`);
- `0x801243B0` (`0x801238F0`), `0x80126958` (`0x801268BC`);
- `0x8012FC24`, `0x8012FCF8`, `0x8012FD48`, and `0x8012FD90`
  (all in `0x8012F500`);
- `0x80130278`, `0x801302E0`, and `0x80130360`
  (all in `0x8012FDF8`);
- `0x80138E5C` (`0x80138B60`), `0x801472CC` (`0x80147124`),
  `0x80147E58` (`0x80147760`);
- `0x80153580` (`0x80153400`), `0x80153BB8` (`0x80153A68`);
- `0x80159A88` (`0x801599C4`), `0x8015B24C` (`0x8015B188`),
  `0x8015B500` (`0x8015B44C`), `0x8015B97C` (`0x8015B904`), and
  `0x8015BA2C` (`0x8015B9D0`).

The broad caller set is consistent with a shared flush point used by multiple
scene/UI paths rather than ownership by one race-scene routine.

## Rejected interpretations

- It is not a queue producer: it never increments `0x801762D0` or writes the
  parallel arrays, and it clears the count after consumption.
- It is not a single-object renderer: both loops are count-bounded, and three
  mode-selected wrappers delegate actual mesh transformation and GPU packet
  submission.
- The auxiliary array is not used for modes 0 or 2; only the mode-1 wrapper
  receives its corresponding word.
