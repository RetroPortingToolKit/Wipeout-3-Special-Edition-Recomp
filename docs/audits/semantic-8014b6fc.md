# Semantic audit: `0x8014B6FC` / `UpdateLoadingMessageCallback`

## Result

`0x8014B6FC` is the no-argument loading-screen callback now named
`UpdateLoadingMessageCallback`:

```c
void UpdateLoadingMessageCallback(void);
```

It uses a reentry guard, blinks an underscore on a shared loading message over
a 48-callback cycle, and services the common frame/display routine on every
accepted invocation.

## Exact boundary, ABI, and return

The complete owner is `[0x8014B6FC,0x8014B830)`, exactly `0x134` bytes, matching
the generated range table. Entry creates a 0x60-byte frame and saves
`ra/s0/s1/s2`; all paths converge on the restore at `0x8014B818..0x8014B82C`.
The next independent owner creates its own frame at `0x8014B830`.

No incoming argument register is read before being overwritten. The callback
therefore has a void/void ABI. The early guarded path leaves an address-building
constant in `v0`, while the normal path leaves the result of `0x8012115C`;
neither is consumed by the callback dispatcher. There is no semantic return.

## Registration and control-flow proof

There are no direct external `j` or `jal` calls to the owner. Instead,
`0x8014BA84..0x8014BA8C` synthesizes its address as
`0x80150000 - 0x4904 = 0x8014B6FC` and passes it to `0x801208EC`. That setter
stores the callback at `0x80175D44` under the shared critical-section helpers.
The dispatcher loads that word at `0x801202A0`, tests it, and invokes it via
`jalr` at `0x801202B0`. The paired shutdown routine passes null to the setter at
`0x8014BAB8..0x8014BAC0`.

An exhaustive aligned scan finds only one direct `j`/`jal` whose destination
lies inside the range: the owner's own `j 0x8014B80C` at `0x8014B800`. There
are no external interior targets and no competing callable entry.

## State machine and rendering formula

The private state is:

```text
8017834C  u8   reentry guard
8017834E  s16  blink phase
8017C1BC  ptr  shared loading-message string
```

The callback implements:

```c
if (guard != 0)
    return;
guard = 1;

if (phase == 0) {
    begin_loading_frame();
    DrawFontString(message, 28, 48, 15);
    set_text_rgb(107, 99, 90);
    finish_loading_frame(-1);
    present_loading_frame();
}

if (phase == 24) {
    begin_loading_frame();
    DrawFontString(message, 28, 48, 15);
    int n = FormatString(local, "_");
    DrawFontString(local, n + 29, 48, 15);
    set_text_rgb(107, 99, 90);
    finish_loading_frame(-1);
    present_loading_frame();
}

phase = (phase == 48) ? 0 : (uint16_t)phase + 1;
service_frame_display();
guard = 0;
```

The phase comparison is signed (`lh`) while the increment source is unsigned
(`lhu`); under normal initialization at zero it visits 0 through 48 and then
wraps to zero. Thus the two render states occur 24 callbacks apart. The literal
at `0x8016E9E0` is exactly the NUL-terminated string `"_"`.

The busy path returns without changing phase or calling any callee. On the
normal path the guard is cleared only after `0x8012115C`, so nested entry during
the delegated work is suppressed.

## Callees and effects

- `DrawFontString` (`0x80105B78`) renders the shared message and optional
  underscore at y=48 with style 15.
- `FormatString` (`0x8015C698`) materializes the underscore in the local
  buffer and supplies the horizontal offset through its count return.
- `0x800EEAD0` stores the RGB triplet `(107,99,90)` in shared text state.
- `0x800EE58C`, `0x800EEA44`, and `0x800EE9CC` bracket the two loading-frame
  redraws; `0x8012115C` is invoked on every accepted callback.

The exact deeper identities of the remaining display helpers are not required
to prove this owner's callback, phase, text, or state semantics and are left
unnamed here.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for the
  exact disassembly, aligned target scan, address construction, and globals;
- `generated/SCES_028.45_full.ranges` for the exact 0x134-byte owner range;
- callback registration/setter/dispatcher windows at `0x8014BA40`,
  `0x801208EC`, and `0x8012029C`;
- recovered `DrawFontString` and `FormatString` symbols plus the literal bytes
  at `0x8016E9E0`.
