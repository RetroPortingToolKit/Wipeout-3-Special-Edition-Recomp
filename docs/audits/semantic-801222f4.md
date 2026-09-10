# Semantic audit: `0x801222F4`

## Result

`0x801222F4` is the confirmed audio control `SetGlobalStereoWidth`. It clamps a
byte-truncated width to 0..127, records it in global audio state, and, when the
active backend is enabled, submits symmetric positive/negative Q8 endpoints
to the audio command dispatcher.

Its recovered ABI is:

```c
void SetGlobalStereoWidth(uint8_t width);
```

The function has no semantic return value.

## Boundary and control flow

The generated owner is exactly `[0x801222F4,0x80122378)`, `0x84` bytes. It
opens a 48-byte frame and saves `ra`. Every guard and the command-dispatch path
join the epilogue at `0x80122368`; the return is at `0x80122370` with frame
restore at `0x80122374`. The next independent owner begins at `0x80122378`.

Dispatch entries at `0x8012230C`, `0x80122320`, `0x80122328`, `0x8012232C`,
`0x8012233C`, and `0x80122368` are internal guard, clamp, or post-call blocks.
No direct caller targets an interior instruction.

The only callee is the retained audio command dispatcher at `0x801656E0`.

## Guards, clamp, and state

The input is first truncated with `width &= 0xFF`. If global audio-enable byte
`0x80175E68` is zero, the routine returns without any state change. It then
requires secondary enable byte `0x80175E69`; zero there is also a complete
no-op.

For enabled audio, values 0..127 pass unchanged and values 128..255 clamp to
127. The clamped byte is always stored at `0x801D6E54`. If backend-enable byte
`0x80175E6A` is zero, processing stops after that retained-state update.

When all three enable bytes are nonzero, the function constructs this stack
command record:

```c
struct {
    uint32_t command; // 6
    uint32_t unused;
    int16_t positive; // +(width << 8)
    int16_t negative; // -(width << 8)
};
```

It passes the record address to `0x801656E0`. The equal-magnitude opposing
signed endpoints establish stereo-width/separation semantics rather than an
ordinary master-volume level. The routine itself does not write SPU registers;
hardware/backend effects are delegated to the command dispatcher.

## Caller census and duplicate accounting

Ten generated call instances collapse to six unique executable sites:

```text
0x800DC1D0
0x8012AE90
0x8012BAD8
0x80136308
0x80137580
0x801375B0
```

`0x800DC1D0` appears in four static overlay images. `0x8012AE90` appears in two
coverage copies. Each other address appears once. These are duplicate generated
representations, not additional guest call sites. All six logical callers
target the true entry and none targets an interior continuation.

The conservative name describes the directly evidenced symmetric stereo
control. It does not assign an unproven Psy-Q SDK identity to the large private
dispatcher at `0x801656E0`.
