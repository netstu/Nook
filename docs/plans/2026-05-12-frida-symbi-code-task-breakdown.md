# 2026-05-12 Frida Symbi Code Task Breakdown

## Goal

Turn the previous Frida-vs-Nook alignment analysis into concrete Nook code tasks.

This document is intentionally code-facing:

- which file
- which function
- what to tighten
- why it matters
- suggested priority

It is not a broad architecture essay.

## Scope

This breakdown focuses on the current `spawn` path only:

- `symbi` injection/gating
- spawn backend routing
- authoritative runtime-ready state

Out of scope for this round:

- SELinux policy patch mainline work
- full `zygote-control` resurrection
- hook engine S1-S4 performance work

## Track A: Symbi Injection Layer

Primary file:

- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)

### A1. Shrink remote stub dependency surface

Relevant code:

- `collect_symbi_context()`
- `write_stub_and_patch_slot()`

Current shape:

- the stub config currently carries many remote helper function pointers:
  - `socket`
  - `connect`
  - `write`
  - `read`
  - `close`
  - `getuid`
  - `getpid`
  - `getppid`
  - `raise`
  - `__android_log_print`

Why this matters:

- every extra remote symbol is one more zygote-side dependency
- every dependency increases ROM/version sensitivity
- Frida's current direction is to keep the zygote-side payload as narrow as possible

Task:

- review which helper pointers are strictly required for:
  - target child identification
  - callback handshake
  - one-shot restore safety
- remove non-essential helpers from `TStub`
- keep logging/debug helpers out of the hot path when possible

Expected outcome:

- smaller stub surface
- fewer remote resolution failure modes

Priority:

- `P0`

### A2. Separate "gating payload" concerns from "child runtime delivery" concerns

Relevant code:

- `inject_spawn_symbi_by_pids()`
- `collect_symbi_context()`
- caller path passing `so_path`

Current risk:

- the `symbi` path still conceptually bundles two concerns:
  - zygote-side patch/gate
  - child-side agent delivery

Why this matters:

- previous failures happened exactly at this boundary
- zygote-owned delivery state proved unsafe on Android 11 whitelist enforcement

Task:

- make the code paths structurally explicit:
  - zygote patch and callback path
  - child payload delivery path
- document the boundary in code comments where handoff occurs
- avoid mixing child delivery fallback decisions into zygote patch logic

Expected outcome:

- cleaner failure isolation
- easier future memfd-first refactor

Priority:

- `P0`

### A3. Make restore sequencing auditable and single-purpose

Relevant code:

- `restore_write_mem()`
- `restore_original_slot()`
- `restore_original_slot_ptrace()`
- post-callback restore section inside `inject_spawn_symbi_by_pids()`

Current shape:

- restore logic is already better than before
- but it is still spread across multiple functions with mixed stop/write/resume semantics

Task:

- make restore flow read like one strict state machine:
  - patch installed
  - target app started
  - callback observed or timeout
  - primary restore attempt
  - fallback restore attempt
  - final error classification
- keep normal restore and ptrace restore behaviorally equivalent where possible
- ensure all restore failure logs clearly distinguish:
  - write failure
  - stop failure
  - callback timeout after successful restore

Expected outcome:

- easier debugging after real-device failures
- lower risk of "restore happened but logs look ambiguous"

Priority:

- `P0`

### A4. Reduce zygote stop window

Relevant code:

- `inject_spawn_symbi_by_pids()`

Current shape:

- zygote is stopped
- remote mem is opened
- stub and slot are patched
- zygote is resumed

Task:

- measure and minimize work done while zygote is stopped
- precompute everything possible before `stop_process(zygote_pid)`
- keep only the minimum write operations inside the stop window

Expected outcome:

- lower zygote disturbance
- closer to Frida's "small gate payload, short critical section" spirit

Priority:

- `P1`

## Track B: Spawn Backend Routing Layer

Primary file:

- [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)

Implementation file is the corresponding `.cpp`.

### B1. Make backend responsibilities explicit in code, not just docs

Relevant code:

- `SpawnBackend`
- `SpawnViaSymbi()`
- `SpawnViaLegacyNcore()`
- `TrySpawnViaZygoteControl()`
- `FinalizeLegacySpawn()`
- `FinalizeZygoteControlSpawn()`

Current risk:

- backend semantics are known from recent docs, but not always obvious from the code surface

Task:

- add short comments around backend responsibilities:
  - `kSymbi` = zygote gate path
  - `kLegacyNcore` = stable prepare/clear semantic path
  - `kZygoteControl` = experimental agent-controlled path
- make fallback intent obvious at call sites
- keep "default path" and "explicit request path" easy to audit

Expected outcome:

- fewer routing misunderstandings
- easier future maintenance after context compression

Priority:

- `P0`

### B2. Unify embedded delivery policy for agent and ncore

Relevant code:

- `EnsureLegacyNcoreReady()`
- `EnsureLegacyAgentReady()`
- `MaybeCleanupLegacyNcoreArtifact()`
- `MaybeCleanupLegacyAgentArtifact()`

Current risk:

- agent and ncore lifecycle are conceptually similar but still handled as separate special cases

Task:

- review whether these functions can share a narrower common policy helper:
  - embedded-first
  - memfd-first when possible
  - file fallback only when explicitly needed
  - cleanup behavior clearly tied to materialization policy

Expected outcome:

- fewer divergent artifact-handling rules
- lower chance of one backend getting stale behavior

Priority:

- `P0`

### B3. Keep explicit symbi isolated from default stable fallback behavior

Relevant code:

- `IsExplicitSymbiSpawnRequested()`
- `Spawn()`
- backend selection logic in the `.cpp`

Current risk:

- explicit `--spawn-symbi` and default spawn are used for different purposes:
  - explicit `symbi` is an experiment/control path
  - default spawn is the user-facing stable path

Task:

- ensure explicit `symbi` diagnostics remain distinct in logs
- ensure fallback rules are obvious and intentional
- do not let experimental-path fixes silently mutate stable-path semantics

Expected outcome:

- better experimental hygiene
- less accidental default-path churn

Priority:

- `P1`

## Track C: Authoritative Runtime State Layer

Primary files:

- [server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)

### C1. Keep runtime-ready as the only script-capable boundary

Relevant code:

- `HandleAgentReady()`
- `IsRuntimeReady()`
- `RegisterAgentSession()`
- `StoreAgentReadyFrame()`

Current shape:

- this is already partially fixed
- runtime-ready is authoritative

Task:

- keep all script-capable routing decisions anchored to runtime-ready only
- audit for any remaining code paths that still treat early control-stage signals as equivalent

Expected outcome:

- lower risk of binding host operations to the wrong process/session

Priority:

- `P0`

### C2. Keep old session cleanup identity-safe everywhere

Relevant code:

- `RemoveAgentSessionByPidIfMatches()`
- caller logic in `HandleAgentReady()`

Current shape:

- the registry already has identity-aware removal

Task:

- audit all shutdown/close/disconnect paths for pid-only cleanup assumptions
- ensure no late old connection can erase newer authoritative state

Expected outcome:

- fewer repeated-run regressions
- stronger spawn/attach continuity

Priority:

- `P0`

### C3. Make replay behavior reflect authority, not transport noise

Relevant code:

- attach replay in `HandleAttachRequest()`
- cached ready replay
- cached script message replay

Current risk:

- transport-level duplication can still confuse debugging, even when correctness is improved

Task:

- keep replay tied to authoritative runtime frame only
- audit whether control-stage or non-authoritative messages can still be replayed accidentally
- make logs say whether replay source is authoritative cached runtime-ready or not

Expected outcome:

- cleaner repeated attach/spawn debugging
- less confusion from transport-layer symptoms

Priority:

- `P1`

### C4. Add stronger negative tests around wrong-source readiness

Relevant areas:

- `tests/communication/test_server_handlers.cpp`
- `tests/communication/test_session_registry.cpp`

Task:

- add tests for:
  - control-stage ready arrives, runtime-ready never arrives
  - stale control session closes after runtime binding
  - runtime-ready pid/name differs from early control-stage process identity
  - cached replay after repeated spawn still uses authoritative child runtime session

Expected outcome:

- less chance of reintroducing the same class of state bugs

Priority:

- `P0`

## Suggested Execution Order

### Phase T1

State and test hardening first:

- `C1`
- `C2`
- `C4`

Reason:

- this gives the strongest regression guardrails before touching injection internals again

### Phase T2

Symbi critical-path cleanup:

- `A1`
- `A2`
- `A3`

Reason:

- this directly reduces zygote-side fragility while keeping the same architecture

### Phase T3

Delivery-policy cleanup:

- `B1`
- `B2`
- `B3`

Reason:

- this reduces backend drift and artifact confusion

### Phase T4

Optional refinement:

- `A4`
- `C3`

Reason:

- useful, but should come after the more correctness-critical work

## What Not To Do In This Task Set

Do not expand this task set into:

- SELinux patching mainline work
- full `zygote-control` redesign
- hook engine performance rewrite
- broad CLI/tooling feature work

Those are separate tracks.

## Short Conclusion

The most valuable next code work is not to replace Nook's current `symbi` direction.

It is to make the existing direction:

- narrower at the zygote boundary
- more uniform in delivery policy
- stricter in runtime authority semantics
- better protected by regression tests

That is the most direct way to borrow the right parts of Frida without destabilizing the first working default spawn path.
