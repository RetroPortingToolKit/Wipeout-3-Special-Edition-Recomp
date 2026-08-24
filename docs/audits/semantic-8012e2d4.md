# Semantic audit: `0x8012E2D4`

## Result and ABI

`0x8012E2D4` is the confirmed resource-loader adapter
`LoadResourceBySignedIndex`:

```c
void *LoadResourceBySignedIndex(int16_t index, uint32_t *size_out);
```

The machine ABI receives `index` in `a0` and `size_out` in `a1`. The first two
instructions explicitly truncate and sign-extend `a0` through shifts by 16;
therefore values outside the signed-halfword domain do not survive. `a1` is
passed unchanged. The returned `v0` from the private core is returned unchanged
and all callers treat it as a nullable pointer.

## Exact owner and targets

The generated range table proves `[0x8012E2D4,0x8012E310)`, exactly `0x3C`
bytes. The prologue opens a 32-byte frame at `0x8012E2D4`; the call is at
`0x8012E2F8`; `0x8012E300` is its sole continuation. The wrapper returns at
`0x8012E308` and restores the frame in the delay slot at `0x8012E30C`. The
independent byte-swap leaf begins at `0x8012E310`.

No generated JAL or jump targets `0x8012E300`. It is only the return point for
the nested call, not a public entry alias or false split.

## Exact adapter behavior

After sign-extending the index, the wrapper constructs this call to the private
resource core at `0x8012CD44`:

```c
return ResourceCore(0, 0, 0, NULL, (int16_t)index, size_out);
```

It zeros `a0`, `a1`, `a2`, and `a3`; writes the signed index as stack argument
five at caller `sp+0x10`; and writes `size_out` as stack argument six at
`sp+0x14`. The core is the large archive/resource path that performs lookup,
optional allocation/copy/decode, and returns the loaded pointer. This wrapper
adds no allocation policy of its own, performs no null handling, does not write
through `size_out` itself, and preserves the core's nullable result exactly.

The resource identity is independently supported by callers: they pass signed
halfword resource identifiers, provide stack output words for the returned
size, retain `v0` as a pointer, and commonly branch on null immediately after
the call. The name avoids claiming an unproven archive-table or asset-class
term.

## Exhaustive caller census

There are six generated instances and six unique direct call instructions:

```text
80110A48  (owner 801109BC)
80131BE0  (owner 8013199C)
80131DE0  (owner 8013199C)
80131E80  (owner 8013199C)
80131F00  (owner 8013199C)
801321F8  (owner 8013199C)
```

All target the exact entry. There is no overlay duplicate and no interior
target. At `0x80110A48`, `a1` is a stack word at `sp+0x18`; the five calls in
`0x8013199C` likewise reload stack output-word pointers while their indices
come from signed halfwords or explicit zero. The returned pointer is retained
or tested, confirming the pointer-return ABI rather than a void command.

## Confidence

`confirmed`: exact boundary, signed-halfword truncation, six-argument core
mapping, unchanged return, unique caller census, caller pointer use, and lack
of interior targets are instruction-proven. The internals of `0x8012CD44` are
outside this small adapter audit, so its original source name is not asserted.
