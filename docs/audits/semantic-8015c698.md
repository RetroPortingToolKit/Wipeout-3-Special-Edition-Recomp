# Semantic audit: `0x8015C698`

## Result

`0x8015C698` is the real variadic formatting entry `FormatString`, equivalent
to the conventional `sprintf(destination, format, ...)` interface. The
generated 12-byte function is only its argument-spill prefix; it falls through
unconditionally into the generated piece at `0x8015C6A4`.

## Owner and boundary proof

The entry's three instructions store incoming `a1`, `a2`, and `a3` at the
standard caller argument-save slots `sp+4`, `sp+8`, and `sp+12`. There is no
return or transfer. Execution immediately reaches `0x8015C6A4`, which allocates
a 592-byte frame and implements the formatter. Its shared epilogue restores
that frame at `0x8015CF18`; the semantic owner is therefore the half-open range
`[0x8015C698, 0x8015CF1C)`, 2,180 bytes in total.

The apparent `R 8015C698 C` plus `R 8015C6A4 878` division is an AOT shard/split
artifact, not two source-level routines. The next unrelated small callable
begins at `0x8015CF28` after padding.

## Caller proof

An aligned scan of the authenticated executable finds 57 direct calls to
`0x8015C698` and none to `0x8015C6A4`:

`800E6BCC 800E7068 800E7DC0 800E7F4C 800E8300 800FE300 800FE35C
800FE3A0 800FE9D8 800FEA44 800FEA94 800FEAD8 800FEB14 800FEB40
800FEBAC 800FEC2C 800FEC54 800FEC98 800FED04 800FED2C 800FED78
800FEDE8 800FEE2C 800FEFAC 800FEFDC 800FF00C 800FF01C 800FFECC
800FFEE4 80100FCC 8010100C 801013D0 80101414 80102244 8010840C
80111D54 8011705C 80117180 80117208 80117354 801173C8 8011751C
80117590 8012866C 80128714 80129A90 8012CDB8 8012CE18 8012CE60
8012D298 8012D984 8012E3E4 80130C9C 80131428 801317F0 80140154
8014B7B0`.

This is decisive ABI evidence: callers enter the argument-spill prefix, while
the formatter body is never independently invoked.

## Formatting behavior

The body walks the NUL-terminated format string, copies ordinary characters to
the destination, consumes the saved register/stack varargs, and implements
width, precision, sign, padding, integer-base, character, string, and floating
format paths. It terminates the destination and returns the produced character
count. The large local frame holds conversion scratch rather than persistent
state.

The catalog intentionally uses the descriptive `FormatString` name: the ABI
and behavior are confirmed as `sprintf`-equivalent, while no retained literal
proves the exact historical source identifier.

## Confidence

Status is `confirmed`. The variadic spill convention, unconditional
fallthrough, unique entry-call census, format parser, output termination, return
count, and shared epilogue prove both identity and owner boundary.
