# Release freshness audit — 2026-08-22

## Result

The shipped Release executable is stale relative to the latest canonical
native/PGXP/PAL100 work. No fresh rebuild or unbounded static build was started
by this audit.

## Evidence

| Item | Observation |
| --- | --- |
| Canonical source worktree | `C:\Users\Shokunin\Documents\ChatGPT\wipeout3se`, `master`, with in-flight native/PGXP/PAL100 changes |
| Latest relevant source commit observed | `61e4bb5`, `psxrecomp: promote captured high-RAM continuation` |
| Release EXE | `build-release/Wipeout_3_Special_Edition__Recompiled.exe`, 92,883,456 bytes, last write 2026-08-22 17:22:50 -0700 |
| Release EXE SHA-256 | `E01A42C976EC68ACA22D214D6383ECD969B32E0872F52E064863CDD95FECF087` |
| Candidate `build-main-pgxp` | Contains no top-level executable; it is not a shipped artifact |
| Packaged ZIP | `dist/wse-0.1.0-windows-x64.zip`, 159,057,920 bytes, last write 2026-08-22 17:33:54 -0700 |

## Bounded verification

The existing static release verifier was run against `build-release`. The tree
is not current with the present release policy: it predates the auto-enabled
PAL100 state and the current exact package allowlist. The stale tree must not be
treated as the current executable.

## Conclusion

A fresh bounded Release build and one-folder packaging pass are required before
shipping. The generated static overlay is approximately 192 MB of C source and
the prior UI-enabled build exhausted roughly 11 GB while compiling it, so the
next build must use the bounded native path and explicit memory guard rather
than retrying the same unbounded build.
