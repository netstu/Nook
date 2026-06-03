# 2026-05-12 Symbi Convergence Phase Summary

## Purpose

This document consolidates the `A1 / A2 / A3` symbi convergence work completed under:

- [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md)

The goal of this summary is to replace a growing set of narrow status notes with one stage-level view before work returns to the broader spawn mainline.

## What Was Completed

### A1. Remote Stub Dependency Surface Was Reduced

Relevant status notes:

- [2026-05-12-symbi-stub-helper-surface-reduction-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-symbi-stub-helper-surface-reduction-status.md)
- [2026-05-12-symbi-fire-and-stop-callback-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-symbi-fire-and-stop-callback-status.md)

Concrete outcomes:

- removed `getppid`
- removed callback `ppid`
- removed callback package payload
- removed remote `read`
- changed callback from `notify-and-wait-ack` to `fire-and-stop`

Current hot-path remote helper set is now:

- `getuid`
- `getpid`
- `socket`
- `connect`
- `write`
- `close`
- `raise`

This is materially closer to a minimal zygote-side helper contract than the earlier stub surface.

### A2. Gate vs Child Delivery Boundary Was Made Explicit

Relevant status note:

- [2026-05-12-symbi-gate-child-handoff-boundary-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-symbi-gate-child-handoff-boundary-status.md)

Concrete outcomes:

- removed `so_path` from local symbi gate APIs
- `symbi_injector_local.cpp` now owns only:
  - zygote gate preparation
  - callback listener setup
  - callback wait
  - zygote restore
  - child pid handoff
- `ninjector_compat.cpp` explicitly owns:
  - child runtime delivery
  - sidecar `dlopen` path for `SpawnViaSymbi()`
  - memfd child delivery for `SpawnViaSymbiEmbedded()`
  - child resume

This removed a misleading API-level coupling between zygote gate work and child runtime delivery.

### A3. Restore Flow Was Made Auditable and More Single-Purpose

Relevant status notes:

- [2026-05-12-symbi-restore-state-machine-surface-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-symbi-restore-state-machine-surface-status.md)
- [2026-05-12-symbi-restore-driver-unification-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-symbi-restore-driver-unification-status.md)

Concrete outcomes:

- added explicit `SymbiHandoffState`
- added state-name / state-advance helpers
- made handoff progression visible in logs:
  - gate installed
  - target app started
  - callback observed
  - primary restore attempted
  - ptrace restore attempted
  - restore completed
- unified restore primitive skeleton with:
  - `RestoreDriverOps`
  - `run_restore_attempt()`

This did not change restore policy, but it made the restore path much easier to audit and reason about.

## What Was Also Completed Around Spawn Routing

These were not part of Track A itself, but they matter for how the symbi work now sits in the larger spawn path:

- [2026-05-12-runtime-artifact-policy-unification-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-runtime-artifact-policy-unification-status.md)
- [2026-05-12-runtime-artifact-host-test-alignment-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-runtime-artifact-host-test-alignment-status.md)
- [2026-05-12-spawn-backend-responsibility-clarification-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-spawn-backend-responsibility-clarification-status.md)

Net effect:

- spawn backend responsibilities are clearer in code
- host tests now align with the current backend policy instead of stale assumptions
- runtime artifact handling is more unified between agent and ncore preparation

## Current Symbi Shape

After this convergence phase, the current symbi path can be summarized as:

1. collect zygote-only gate context
2. prepare callback listener
3. prepare stub patch
4. install temporary zygote gate
5. start target app
6. receive child callback
7. restore zygote state via primary path or ptrace fallback
8. return stopped child pid to compat layer
9. let compat layer perform child runtime delivery
10. resume child

That separation is now encoded in the code surface instead of being spread across implicit assumptions.

## What Remains Open

### A4 Is Still Open

The stop-window tightening track is not fully finished as a dedicated phase summary outcome.

What remains:

- verify whether more work can be moved out of the zygote stopped section
- ensure no new helper-resolution or allocation work drifts back into the stop window

### Symbi Is Cleaner, But Still Experimental

Even after these improvements, symbi should still be treated as an experimental zygote gate path, not as the unquestioned default stable backend across all environments.

The work completed here improved:

- readability
- boundary clarity
- restore auditability
- helper surface size

It did not yet prove universal device robustness by itself.

## Recommended Next Step

Return to the spawn mainline and use this cleaned symbi shape as input, not as the final destination.

Recommended focus order:

1. re-anchor default stable spawn path vs experimental path in code and docs
2. keep `zygote-control` clearly separate while it remains experimental
3. decide whether symbi now graduates to a more central role in routing, or stays behind explicit policy/flag boundaries
4. only then revisit any remaining `A4` micro-optimizations if real-device evidence shows they still matter

## Bottom Line

This convergence phase did not try to “finish symbi forever”.

What it did accomplish is narrower and more useful:

- the stub is smaller
- the zygote gate API is cleaner
- child delivery responsibility is explicit
- restore flow is readable as a state-driven process

That is enough to stop paying repeated structural debt and move back to the higher-value spawn coordination questions.
