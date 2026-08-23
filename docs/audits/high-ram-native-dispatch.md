# 8 MB high-RAM native-dispatch audit

Date: 2026-08-22

## Finding

The observed `0x8078xxxx` route is a real high-RAM execution path, but it is
not a fully native path. `game.toml` declares 46 roots at `0x80780000` and
nearby addresses. These are trampoline entry points extracted from patched
text jumps; they are not a complete function-boundary map. The generator can
root those stubs, which prevents the entire relocated area from being treated
as interpreter-only, but root coverage is not instruction coverage.

The retained run evidence records `last_store_pc=0x80780AF8` and
`autocompile_degraded=0`, proving high-RAM execution and no broad
autocompile degradation. It also records an `interp_unsupported` guard at
`0x80781474`. That PC is inside the declared high window, but is not one of
the declared roots. The concrete interpretation is a static native-overlay
coverage hole (or an unsupported instruction/dispatch boundary), not a 2 MB
RAM fallback.

## Dispatch chain

```text
patched guest J -> high-RAM trampoline root
                 -> statically generated overlay when root/target is covered
                 -> interpreter guard/bridge at unsupported or uncovered point
```

The overlay declaration is intentionally narrow: it supplies roots for
detour labels that cannot be discovered as callable functions. It does not
teach the static compiler how to generate every target reached by those
stubs, nor does it remove guards inserted for instructions the generator
cannot lower. Runtime dispatch evidence can therefore show that the region is
active while still reporting an interpreter fallback in the same region.

## Why 900% cannot rescue this

The configured 900% value reduces the emulated CPU retirement charge (roughly
one ninth per retired guest instruction). It does not multiply host-side
dispatch, interpreter decoding, guard/bridge bookkeeping, CRTC, SPU, CD, or
DMA service. A native block benefits from the cheaper retirement accounting;
an instruction that exits native dispatch pays the interpreter/bridge cost and
still has to be decoded and routed. If the hot loop repeatedly crosses that
boundary, the fixed dispatch work can dominate the reduced guest-cycle cost.
Thus “900% enabled” and “high-RAM code executed” do not imply “900% native
performance”; the observed guard is sufficient to invalidate that claim.

## Bounded regression check

`tools/audit_high_ram_native_dispatch.py` performs a finite, static check. It
does not run `compile_overlays.py` or any generator. It validates that the
checked-in roots are unique and inside the high window, and can correlate one
retained JSON report with the known guard:

```text
python tools/audit_high_ram_native_dispatch.py
python tools/audit_high_ram_native_dispatch.py --report <finite-run-report.json>
```

The diagnostic is a coverage tripwire, not proof that the guard is the only
fallback. Closing the gap requires a bounded generated dispatch artifact or a
runtime counter comparing native entries with interpreter bridges; neither is
present in this checkout.

## Evidence boundary

The pinned `psxrecomp` submodule is uninitialized and its recorded object is
not available from the configured remote. Accordingly, this audit does not
claim line-level behavior for the missing generator/runtime sources. It uses
the checked-in overlay declaration, generator documentation/comments, prior
retained run report summary, and hardware-parity audit. No runtime core,
shared configuration, generated overlay, or unbounded generator run was
changed or performed.
