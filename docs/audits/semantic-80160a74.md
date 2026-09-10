# Semantic audit: `0x80160A74` / `CdRead`

## Result and canonical ABI

`0x80160A74` is the canonical PsyQ/LibCD read starter `CdRead`:

```c
int CdRead(int sectors, uint32_t *destination, int mode);
```

It waits for any prior read to leave the shared read state, installs the new
sector count, destination, and transfer mode, suppresses callbacks while the
request is owned internally, performs status recovery when required, starts
the lower read engine, and returns normalized boolean acceptance. Completion
is observed separately through the adjacent read-sync routine at
`0x80160C10`.

## Exact owner and ABI

The generated range `[0x80160A74,0x80160C10)` is exact, length `0x19C`. It
creates a 40-byte frame, saves `ra` and `s0..s4`, and has one epilogue at
`0x80160BF0..0x80160C0C`. The next independent framed owner begins at
`0x80160C10`. All generated interior dispatch entries are ordinary loop and
branch continuations.

- `a0` / `s4`: sector count, stored unchanged in read state.
- `a1` / `s3`: destination pointer, stored unchanged.
- `a2` / `s2`: mode flags, stored and then augmented internally.
- `v0`: exactly `(start_result > 0)`, hence zero or one.

This owner does not dereference the destination or directly access CD-ROM or
DMA MMIO; the lower read engine and interrupt callbacks own data movement.

## Previous-read wait and timeout recovery

The active flag is the word at `0x801792AC`. If it is nonzero, `CdRead` samples
the VBlank counter through `VSync(-1)` and spins while the flag remains set and
the unsigned elapsed count is below 121. At 121 ticks it restores the saved
sync callback from `0x801792B0` through `CdSyncCallback`, clears the active
flag, and proceeds with the new request. Thus the wait is bounded to 120
elapsed VBlanks before forced ownership recovery.

## Published read state and transfer geometry

The shared state begins at `0x80179288`. The new request writes:

```text
+0x00  sector count
+0x04  destination pointer
+0x0C  mode, later ORed with 0x20
+0x10  words transferred per sector
+0x1C  VBlank start timestamp
+0x24  active/read-owned flag (managed by the lower engine)
+0x28  saved sync callback
+0x2C  saved ready callback
+0x30  saved CD DMA/data callback when that service is enabled
```

The mode's `0x30` field selects the exact word count:

```text
(mode & 0x30) == 0x00  -> 0x200 words = 2048 bytes
(mode & 0x30) == 0x20  -> 0x249 words = 2340 bytes
otherwise              -> 0x246 words = 2328 bytes
```

The stored mode is then ORed with `0x20`. These sizes and the sector-oriented
caller contract establish CD sector transfer geometry rather than a generic
buffer operation.

## Callback isolation, recovery, and start

The routine exchanges the installed sync and ready callbacks with null through
`CdSyncCallback` and `CdReadyCallback`, saving their previous values in the
read state. When the CD DMA service-enable word at `0x801792CC` has bit zero
set, it likewise suppresses `CdDataCallback` and saves the prior callback.

After recording `VSync(-1)`, it queries current CD status through the internal
status helper at `0x80160E88`. If any status bit in `0xE0` is set, it invokes
confirmed `CdControlWithRetry(9, NULL, NULL)` before starting the read. It then
calls the lower read engine at `0x8016088C` with argument zero and returns one
iff that engine's signed result is positive. Callback restoration and terminal
completion are owned by the read interrupt/sync path, not by this start call.

## Complete caller and interior-target census

An exhaustive aligned scan finds exactly six direct entry calls:

```text
80112D6C 80112E54 80113150 801132A4 8011372C 80164770
```

Five game-side loaders pass one or four sectors, a destination buffer, and
mode `0x80`; each then polls the adjacent `0x80160C10` read-sync routine until
completion. The library-side caller at `0x80164770` likewise follows a command
2 seek/setup with `CdRead` and then calls read-sync. These pairings prove the
canonical read-start role and distinguish it from `CdControl`, `CdControlF`,
and the lower command engine.

No external direct jump or JAL targets any interior instruction. The only
direct interior jumps are the owner's own control-flow joins at `0x80160AF0`,
`0x80160B58`, and `0x80160B64`.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact
  disassembly, state offsets, caller arguments, and exhaustive targets;
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_full_52.c` for the exact owner and full flow;
- audits of `CdSyncCallback`, `CdReadyCallback`, `CdControlWithRetry`,
  `CdControl`, and `CdControlF` for independently recovered LibCD contracts.
