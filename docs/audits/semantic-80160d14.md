# Semantic audit: `0x80160D14` / `ExchangeCdInterruptCallback`

## Result and exact range

`0x80160D14` is the callback exchange leaf conservatively named
`ExchangeCdInterruptCallback`:

```c
void *ExchangeCdInterruptCallback(void *callback);
```

It owns exactly `0x80160D14..0x80160D27` (0x14 bytes, five instructions). The
next owner, confirmed canonical `CdInit`, begins at `0x80160D28`; there is no
padding or generated split between them.

The instructions load the word at `0x801792C8` into `v0`, latch `ra` as the
return target, and store incoming `a0` back to the same word in the `jr ra`
delay slot. The old pointer is therefore returned unchanged while the new
pointer becomes active. There is no validation, interrupt masking, callback
invocation, or hardware access in this leaf. Passing null clears dispatch.

The callback role is independently established by `CdInit`: on successful
initialization it writes internal handler `0x80160E44` to `0x801792C8` and
clears the adjacent companion word `0x801792CC`. The conservative name avoids
confusing this internal interrupt callback with confirmed canonical
`CdDataCallback` at `0x80161338`, which exchanges DMA channel 3 through
`DMACallback` instead.

## Callers

There are exactly two direct JAL sites, both targeting the true entry:

- `0x80116284`, in the CD initialization owner at `0x80115FC0`;
- `0x801381A0`, in confirmed `RestoreCdCallbackConfiguration`.

Both pass null and ignore the returned prior callback. No JAL targets an
interior instruction, and neither call is an overlay duplicate.
