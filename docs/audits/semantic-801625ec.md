# Semantic audit: `0x801625EC` / `DMACallback`

## Result and ABI

`0x801625EC` is the canonical PsyQ DMA callback dispatcher:

```c
typedef void (*DmaCB)(void);
DmaCB DMACallback(int32_t channel, DmaCB callback);
```

It forwards `a0` and `a1` unchanged to the active DMA service's callback
exchange method and returns that method's `v0`, conventionally the displaced
callback pointer, unchanged.

## Exact boundary and target census

The generated range assigns `[0x801625EC, 0x8016261C)`, exactly `0x30` bytes,
to this owner.  It opens a 24-byte frame, saves only `ra`, performs one `jalr`,
restores the frame, returns at `0x80162614`, and executes a NOP return delay
slot at `0x80162618`.  The next owner begins at `0x8016261C`.

An aligned scan of the authenticated executable finds exactly seven direct JAL
sites:

```text
8015ECBC 8015EEEC 8015F554 80161344 80163894 801690B8 8016C3EC
```

There are no direct JALs to an interior instruction.  The backend method call
at `0x80162604` is indirect and therefore is not part of that direct-target
census.

## Dynamic service-table dispatch

The dispatcher loads a service-table pointer from global `0x8017A4EC`, then
loads the function pointer at table offset `+4` and invokes it with the original
register arguments:

```c
DmaCB DMACallback(int channel, DmaCB callback) {
    DmaServices *services = *(DmaServices **)0x8017A4EC;
    return services->exchange_callback(channel, callback);
}
```

There is no null check for the table or method, no channel range check, and no
callback-pointer validation.  Initialization must publish a valid active
service table before any call.  The dispatcher itself neither stores callback
state nor accesses DMA registers; channel-specific storage, interrupt handler
updates, synchronization, and the exact old-pointer result are delegated to
the selected backend method.

The single `jalr` returns to `0x8016260C`; no instruction afterward changes
`v0`.  Thus an emitted/native replacement must preserve arbitrary backend
return bits and must not normalize them to boolean.

## Caller/channel matrix

Observed channel constants are:

| DMA channel | Direct sites | Role established by wrappers |
|---:|---|---|
| 1 | `0x80163894` | MDEC/output-side callback wrapper |
| 2 | `0x8015ECBC`, `0x8015EEEC`, `0x8015F554` | GPU DMA callback install/remove |
| 3 | `0x80161344`, `0x801690B8` | CD-ROM data callback wrappers |
| 4 | `0x8016C3EC` | SPU DMA callback wrapper |

The channel-3 call at `0x80161344` is the audited `CdDataCallback`; it proves
the data callback's path into the generic dispatcher.  Callers install concrete
handlers, pass null to remove them, or forward their own callback argument.
No caller in this executable uses channels 0, 5, 6, or an out-of-range value.

## Confidence

`confirmed`: canonical identity, exact boundary and ABI, exhaustive direct
caller/interior-target census, dynamic table/global/method offsets, unchanged
argument and return propagation, observed channel matrix, and delegation of
all callback/hardware state are instruction-proven.
