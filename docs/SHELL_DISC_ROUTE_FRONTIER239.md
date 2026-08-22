# Shell-to-disc route evidence at frontier 239

## Result

The authenticated shell path remains before its first disc-status call. The
resident shell bytes prove a finite initializer corridor from shell entry
`0x80030000` to the call at `0x80030120` (`JAL 0x800359C4`). Frontier 239 stops
inside that corridor at `0x80050F8C`; no disc predicate has executed and no
hardware defect was independently established.

The canonical route receipt is
`clean-aot/catalog/evidence/bios-shell-disc-route-frontier239.json`, SHA-256
`d9b7d422b3681d5fc2bf7e3a1c830c53ff08cbb41be32ba4d9c61a5aa08852c0`.
It binds resident image SHA-256 `f1efc425...7ea3`, frontier-receipt SHA-256
`6971d244...0452`, terminal PC `0x80050F8C`, and terminal RAM epoch
`4d473637...7a`.

## Goal-directed slice

The byte-proven shell-entry call order before the goal is:

1. `0x8005A8D0`, `0x8005A8E0`, `0x80030798`, `0x800403F0`, and
   `0x8004EF90` establish BIOS services, allocation, shell state, and events.
2. `0x8003FDB0` performs SPU mode-6 initialization. Its selected path calls
   `0x8003FCC8` and `0x8003FEB4`; the latter clears its 4-KiB source buffer,
   performs sixteen `0x80054428` transfers of 4 KiB each, and therefore clears
   exactly 64 KiB of SPU RAM without collapsing device time.
3. The observed event/callback corridor reaches `0x800513A8`, whose delay slot
   sets `a0=1`, and calls `0x80050F8C`. The current routine takes the
   `a0==1` branch through `0x80050FCC` and returns at `0x80051078` to the sole
   direct-call continuation `0x800513B0`.
4. After the enclosing callback returns, shell entry continues through
   `0x8003FE88`, `0x8003F910`, `0x80040490`, shell stage 0, root-counter
   synchronization, event mode 3, CD subsystem/callback initialization, and
   shell stages 1 and 2.
5. The pre-query call `0x80035F50` may return either sign. Both byte-proven
   paths only set state word `0x80079DF0` and rejoin at `0x80030118`; neither
   can bypass the goal call.
6. `0x80030120` calls `0x800359C4` with `a0=1`. The first disc-dependent
   branch is later at `0x80030128`, outside this receipt.

Every control-transfer instruction and architectural delay slot named by the
receipt is re-read from the resident image. Any image, frontier number, terminal
PC, RAM epoch, opcode, direct target, return callsite, or loop range mismatch
causes the tool to reject without output.

## Finite returns and remaining prerequisite

`JR $ra` at `0x80051078` has the unique direct-call continuation
`0x800513B0`. The enclosing `0x80051300` routine has exactly eight resident
direct JAL callers, yielding the finite continuation set emitted in the
receipt. Frontier 239 did not preserve registers or stack words, so selecting
one of those eight as the live saved return would be a guess. Authorization can
use the finite set as CFI, but must resume the authenticated machine stack to
select the live member.

This is the only unresolved control prerequisite in the goal-directed slice.
It is not a reason to authorize all eight paths as executed, synthesize `$ra`,
skip initialization, or force disc state.

## Reproduction

Run `tools/analysis/build_shell_disc_route.py` with the ignored authenticated
resident image at `build-bios-frontier-c0/shell-static.bin`, the committed route
specification, and the committed frontier-239 receipt. The checked-in unit test
proves deterministic output and rejects image, edge, and frontier mutation.

