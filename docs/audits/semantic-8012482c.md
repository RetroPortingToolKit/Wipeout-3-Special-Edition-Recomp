# Semantic audit: `0x8012482C`

## Result

`0x8012482C` is a real callable function and is now named
`InitializeRaceRuntimeResources` with `status = "known"`.

It is an argument-free, one-time startup initializer. It constructs a shared
transform-like object, allocates four race-owned work buffers, invokes several
dependent subsystem initializers, and constructs a 38-byte display/resource
descriptor. The evidence does
not yet justify narrower gameplay names for every allocated buffer, so the
symbol deliberately describes the proven orchestration role.

## Boundary and callers

- Entry: `0x8012482C`, with a conventional 32-byte stack-frame prologue.
- Return: `jr $ra` at `0x8012494C`; the delay slot at `0x80124950` releases the
  frame.
- Semantic extent: `0x8012482C..0x80124953` (`0x128` bytes).
- The next independent prologue/range begins at `0x80124954`.
- The retained executable contains exactly one direct call, a `jal` at
  `0x80116504` in the larger startup routine rooted at `0x80116128`.
- The generated dispatch entries at `0x80124840..0x80124938` are post-call
  continuations, not additional ABI entries.

The boundary is independently supported by
`generated/SCES_028.45_full.ranges` (`F 8012482C`, `R 8012482C 128`) and by the
complete prologue/epilogue pair in `generated/SCES_028.45_full_117.c`.

## ABI

The function consumes no incoming arguments. Before every outgoing call it
constructs fresh `$a0-$a3` values from constants, globals, or prior return
values. It preserves `$s0`, `$s1`, and `$ra` in its 32-byte frame.

The final call to `0x80141D6C` returns a handle in `$v0`. That value is both
published to `0x801879E0` and left in `$v0` at return. No retained caller uses
the register result after the call, so the externally meaningful behavior is
the global publication rather than a proven public return contract.

## Proven initialization sequence

In execution order, the routine:

1. Calls `0x801354F4`, which allocates and initializes an 80-byte object from
   the arena rooted at `0x8018F098`, then stores its pointer at `0x801D734C`.
2. Calls `0x80135574(ptr, 0, 0, 0)` and `0x8013558C(ptr, 0, 0, 0)`. The first
   writes three zero vector components at offsets `+0x14/+0x18/+0x1C`; the
   second consumes those signed components to update the object's transform
   fields. A second `0x801354F4` result is linked through the first object's
   `+0x44` field before those calls.
3. Allocates `0x1554` (5460), `0x0800` (2048), `0x02E0` (736), and `0x0B40`
   (2880) bytes from the same arena using `0x8011686C`. Because each store is
   in the next call's delay slot, the results are published respectively at
   `0x801D7038`, `0x801D74B4`, `0x801D7344`, and `0x801D4B08`.
4. Calls `0x8015B7E4` with the shared transform object. Its return value is not
   retained by this routine.
5. Calls the argument-free initializers at `0x80126BB8`, `0x8011C910`,
   `0x800F0428`, and `0x800F4C40`.
6. Calls `0x80141D6C(0x8016E170, 1)` and publishes the returned 38-byte
   descriptor at `0x801879E0` in the epilogue. `0x8016E170` is the literal
   string `c:\\wipeout3\\common\\shads.bt4`, not a callback. The callee
   overwrites `$a0` without consuming it, so that path argument is unused in
   the retained build; `$a1 = 1` does contribute to initialized flag fields.

The mixture of persistent global allocations, an initialized shared transform,
dependent subsystem setup, and descriptor construction establishes a race-runtime
initialization role. It is not a per-frame renderer: its sole call is on the
startup path before the later race loop, and repeated calls would replace
persistent global allocations.

## Confidence and remaining unknowns

The callable boundary, sole call site, no-argument ABI, allocation sizes,
global destinations, and sequencing are instruction-level facts. `known` is
appropriate because the orchestration role is clear, but `confirmed` would be
premature: the exact domain types of the four buffers and the descriptor service
at `0x80141D6C` remain unnamed. Future audits of those callees can refine this
to a product-specific subsystem name without changing the boundary.

## Evidence locations

- `generated/SCES_028.45_full.ranges`: function/range boundary at
  `0x8012482C`, followed by `0x80124954`.
- `generated/SCES_028.45_full_112.c`: sole direct call at guest `0x80116504`
  within `0x80116128`.
- `generated/SCES_028.45_full_117.c`: complete body at guest
  `0x8012482C..0x80124950` and allocator body at `0x8011686C`.
- `generated/SCES_028.45_full_122.c`: the 80-byte object constructor and its
  vector/transform helpers at `0x801354F4`, `0x80135574`, and `0x8013558C`.
