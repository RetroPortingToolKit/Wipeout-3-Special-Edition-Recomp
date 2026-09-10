# Semantic audit: `0x8012C1A4`

## Result

`0x8012C1A4` is the confirmed argument-free test-and-clear accessor
`ConsumeOneShotFlag80175F90`. It converts pending word `0x80175F90` into a
single successful poll: a nonzero value is cleared and reported as one, while
zero is reported unchanged.

Its recovered ABI is:

```c
int ConsumeOneShotFlag80175F90(void);
```

Equivalent behavior is:

```c
if (flag_80175F90 == 0)
    return 0;
flag_80175F90 = 0;
return 1;
```

The function deliberately treats every nonzero word value identically; it
does not return the prior raw value.

## Logical boundary

The entry is generated as `[0x8012C1A4,0x8012C1C4)`, `0x20` bytes. Its zero
branch transfers to the adjacent generated eight-byte piece at `0x8012C1C4`,
which is only `jr ra; nop` and inherits `v0=0` from the entry's branch delay
slot. This piece has no independent caller or ABI. The exact logical owner is
therefore `[0x8012C1A4,0x8012C1CC)`, `0x28` bytes.

On the nonzero path, `0x8012C1B8` clears the global and returns with `v0=1`
loaded in the `jr` delay slot. The generated dispatch entry at `0x8012C1B8`
is an internal branch continuation. The next independent function begins at
`0x8012C1CC`.

There are no calls, frames, arguments, or hardware accesses in the owner.

## Caller and target census

Seven generated call instances reduce to four unique executable entry sites:

```text
0x801236C0
0x8012A654
0x8012AF84
0x8012B360
```

Duplicated generated instances at the latter three addresses are overlay or
coverage copies, not distinct callers. Every JAL targets `0x8012C1A4`. No JAL
targets `0x8012C1B8` or `0x8012C1C4`; those are reachable only through the
owner's own conditional control flow.

## State evidence and naming limit

The only ordinary state read is the word at `0x80175F90`, and the only possible
write is zero to that same word. The retained code has one observed producer:
`0x8012B9E8` stores one to the flag on a conditional state-update path before
the four consumers poll it. Thus the data flow proves one-shot event/dirty
semantics rather than a persistent mode.

The surrounding large callers do not establish a sufficiently narrow domain
identity for the event itself. The symbol intentionally retains the global
address instead of guessing whether the flag denotes a palette, race, UI, or
other transition. It has no shared effects beyond consuming that one word.
