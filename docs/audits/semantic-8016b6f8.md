# Semantic audit: `0x8016B6F8` / `DelaySpuRegisterAccess`

## Result and ABI

`0x8016B6F8` is a fixed-duration busy delay used by the SPU library around
sound-register accesses.  A conservative descriptive name and ABI are:

```c
void DelaySpuRegisterAccess(void);
```

No argument register is read.  The final loop comparison leaves `v0 = 0`, but
all callers discard it and the routine is state/timing-oriented, so this is
not treated as a semantic return.  The executable has no symbol table proving
a historical SDK spelling such as a private `_spu_*` name.

## Exact boundary and target census

The generated range owns `[0x8016B6F8, 0x8016B754)`, exactly `0x5C` bytes.  It
opens an eight-byte frame, returns at `0x8016B74C`, and restores `sp` in the
delay slot at `0x8016B750`; the next owner begins at `0x8016B754`.

An aligned scan of the authenticated executable finds exactly fourteen direct
JAL sites:

```text
8016ADBC
8016AF54 8016AF5C 8016AF64 8016AF6C
8016AF84 8016AF8C 8016AF94 8016AF9C
8016B020 8016B084 8016B0F8 8016B100
8016B1BC
```

There are no direct calls to an interior instruction in
`[0x8016B6FC, 0x8016B754)`.  Every call lies in the adjacent SPU-control and
transfer library owners.

## Busy-loop behavior

The routine initializes two stack words:

```c
uint32_t accumulator = 13;
int32_t counter = 0;
do {
    accumulator = accumulator * 13; // low 32 bits, modulo 2^32
    counter++;
} while (counter < 60);
```

Both words are reloaded and stored on every iteration.  There are exactly 60
multiplications: after the loop the otherwise unobservable accumulator is
`13^61 mod 2^32`.  There are no calls, global accesses, MMIO accesses,
condition inputs, or early exits.  The stack is the only written memory.

## SPU caller context and state effect

The nearby code accesses halfwords at offsets `+0x180..+0x1AE` from the shared
SPU register-base pointer, including the transfer data port, control register,
and status register.  Calls bracket control-bit changes, follow transfer-port
writes, precede status polling, and appear in groups of four during SPU reset
and initialization.  This placement proves the delay's subsystem role even
though the leaf itself is hardware-agnostic.

The routine's only intended external effect is elapsed guest CPU time between
SPU operations.  It does not acknowledge an interrupt, poll readiness, alter
SPU state, or create an ordering barrier by reading hardware.  Consequently a
native replacement must preserve an appropriate timing/order effect rather
than interpreting the dead accumulator as useful data; the current generated
execution naturally charges its guest instructions.

## Confidence

`confirmed`: exact boundary, no-argument/no-semantic-return ABI, exhaustive
caller/interior-target census, 60-iteration recurrence, lack of persistent
state or direct hardware effects, and SPU-register caller context are
instruction-proven.  A historical private-library spelling is intentionally
not claimed.
