# 2026-05-12 Spawn Owner State Convergence Status

## Goal

Record the owner-state tightening work completed after the initial `zygote-control` logging/state-model phase.

This document focuses on actual code movement in `Spawn()` / `FinalizeSpawn()`:

- ownership boundaries
- active owner semantics
- unified spawn commit flow
- regression coverage added to keep the model stable

It does not propose a new backend architecture. It records the current state so later context compression does not lose the rationale.

## What Changed

### 1. Finalize now follows owned state instead of shared mutable fields

Earlier finalize logic mixed:

- remembered backend
- residual zygote-control targets
- mutable member reads after state clearing

This was tightened in stages:

- `FinalizeZygoteControlSpawn()` stopped depending on `active_spawn_token_` after the caller had already detached it
- `zygote-control` finalize now receives owned transaction state explicitly
- foreign finalize requests no longer steal `zygote-control` teardown
- foreign finalize requests no longer clear legacy-owned spawn state

Result:

- finalize is now owner-record driven
- cleanup is no longer inferred from "whatever shared fields are still non-empty"

### 2. Zygote-control teardown state became a real owned transaction

`zygote-control` now uses:

- `identifier`
- `spawn_token`
- `targets`

as a single owned transaction surface instead of scattered member reads.

Current struct:

- `ZygoteControlOwnedTransaction`

This made two things explicit:

- only the matching owner request may detach the transaction
- fallback finalize probing only applies when residual transaction state belongs to the current request

### 3. Spawn owner metadata moved toward a single-slot owner record

Legacy scattered state:

- `active_spawn_identifier_`
- `active_spawn_token_`
- `active_spawn_ncore_path_`
- `active_spawn_agent_path_`
- `active_spawn_materialized_*`
- `active_spawn_backend_`

was converged into:

- `SpawnOwnedState`

Current fields:

- `identifier`
- `spawn_token`
- `ncore_path`
- `agent_path`
- `materialized_ncore`
- `materialized_agent`
- `backend`

This does not yet support concurrent active spawn owners. It formalizes the opposite:

- the current server is a single active owner model

### 4. Active-owner boundaries are now explicit

The server now rejects new spawn attempts whenever an active owner already exists.

Behavior:

- same identifier: `spawn already active for identifier`
- different identifier: `spawn already active`

This is important because the runtime still stores only one active owner slot. The code and tests now acknowledge that reality instead of silently allowing later spawn attempts to overwrite the slot.

### 5. Failed respawn no longer clears the previous owner by accident

There was a subtle bug:

- first spawn succeeds for identifier `X`
- second spawn for the same identifier fails with a different token
- failure cleanup used to clear owner state by matching only `identifier`

Now failure cleanup only clears active spawn state when both match:

- `identifier`
- `spawn_token`

That means a failed respawn attempt cannot erase the prior successful owner record.

### 6. Spawn success paths now commit through a unified top-level helper

Previously:

- backend helpers populated pieces of active state directly
- `Spawn()` later filled in backend / identifier at separate success points

Now this is partially converged:

- backend helpers return local ownership data
- `Spawn()` builds a local `PendingSpawnCommit`
- `CommitPendingSpawn(...)` performs the shared-state commit

This has been applied to:

- `zygote-control`
- `symbi`
- `legacy ncore`

The behavior did not change. The commit location did.

### 7. Spawn local result state is now grouped as `SpawnOutcome`

`Spawn()` previously tracked multiple independent locals:

- `zygote_control_error`
- `symbi_error`
- `legacy_error`
- pending commit state

These have now been grouped into:

- `SpawnOutcome`

Current fields:

- `pending_commit`
- `zygote_control_error`
- `symbi_error`
- `legacy_error`
- `terminal_primary_backend`
- `terminal_secondary_backend`
- `route_attempt`
- `fallback_policy`
- `final_status`

This is still an intermediate form, but it is now closer to a real route/result state machine.

### 8. Terminal spawn handling is now centralized by explicit final status

Earlier, `Spawn()` still ended in a long branch chain that both:

- classified the terminal result
- emitted the final log/error behavior inline

That mixed "state selection" and "terminal rendering" in one place.

This was tightened by:

- adding `FinalizeSpawnOutcome(...)`
- making it pivot on `switch (outcome.final_status)`
- reducing the tail of `Spawn()` to:
  - compute final outcome fields
  - delegate terminal handling once

Result:

- route/result classification remains inside `Spawn()`
- final terminal logging and error formatting now exits through one helper
- future state-machine work has a stable single exit surface instead of duplicated terminal branches

### 9. Terminal classification is now separate from terminal rendering

The tail of `Spawn()` still previously contained a long "decide final status/backend" branch block.

This was tightened one step further by adding:

- `ClassifyTerminalSpawnOutcome(...)`

Current split is now:

- `Spawn()` performs route attempts and collects raw outcome fields
- `ClassifyTerminalSpawnOutcome(...)` converts those raw fields into explicit terminal state
- `FinalizeSpawnOutcome(...)` renders the final log/error behavior from that state

Result:

- terminal classification and terminal rendering are no longer mixed
- `Spawn()` is shorter and closer to an actual state transition function
- later work can push more route decisions into explicit state without touching final log/error behavior

### 10. Zygote-control fallback eligibility is now isolated from the main spawn flow

The `zygote-control` route previously still decided fallback eligibility inline inside `Spawn()`, mixing:

- strict-mode policy
- lifecycle/failure-state interpretation
- fallback allowance

This was tightened by adding:

- `ShouldAllowZygoteControlFallback(...)`

Current split is now:

- `TrySpawnViaZygoteControl(...)` produces raw route failure
- `ShouldAllowZygoteControlFallback(...)` decides whether the route may fall through
- `ClassifyTerminalSpawnOutcome(...)` handles later terminal classification

Result:

- the `zygote-control` route boundary is more explicit
- strict-mode policy is no longer buried inside the main procedural branch chain
- host-side state is now close to the point where device/runtime `zygote-control` work can proceed with less ambiguity

### 11. Successful spawn commit is now isolated from route branches

The three success paths still previously repeated the same top-level work:

- assign final owner metadata
- populate `pending_commit`
- mark `final_status = kSuccess`
- commit active owner state

This was tightened by adding:

- `CommitSuccessfulSpawnOutcome(...)`

Current split is now:

- each route helper produces local success data
- `CommitSuccessfulSpawnOutcome(...)` converts that into a uniform successful `SpawnOutcome`
- `Spawn()` no longer repeats commit wiring in each backend branch

Result:

- success commit semantics are now uniform across `zygote-control`, `symbi`, and `legacy`
- the host-side spawn flow is materially closer to an explicit route/result state machine
- this is a reasonable stopping point for host-side convergence before switching to device/runtime `zygote-control` work

## Zygote-Control Entry Step

The next step after the host-side convergence work started with a narrow RPC-boundary cleanup in
[server/zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp).

### 12. Install/uninstall RPC dispatch is now centralized

`InstallZygoteForkHook()` and `UninstallZygoteForkHook()` both previously repeated the same outer work:

- log begin
- dispatch RPC
- log success/failure

This was tightened by adding:

- `DispatchZygoteControlRpc(...)`

Current split is now:

- `WaitForZygoteControlReady(...)` owns readiness polling
- `DispatchZygoteControlRpc(...)` owns the outer RPC dispatch/log boundary
- install/uninstall helpers now mainly define method/args and stage semantics

Result:

- `zygote-control` RPC entrypoints now have a cleaner outer boundary
- the next debugging focus can move to real runtime readiness/session timing issues instead of duplicated dispatch shells
- this is the first concrete code step in the actual `zygote-control` track

### 13. Zygote-control RPC now waits for authoritative agent ready, not just socket presence

One concrete race was identified in the real `zygote-control` path:

- agent socket connection could be registered immediately on accept
- `zygote_control_rpc` session lookup could then resolve that bare session
- RPC would be sent before control-stage `AGENT_READY` had established authoritative readiness

This was tightened by:

- adding ready-gated lookup helpers in
  [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
  and
  [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- switching
  [server/zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
  from `WaitForAgentSessionByIdentity(...)` to `WaitForReadyAgentSessionByIdentity(...)`

Result:

- `zygote-control` RPC no longer targets a merely connected socket
- control/runtime `AGENT_READY` now remains the authoritative readiness boundary for RPC dispatch
- the next device-side investigation can focus on actual control-ready timing, not premature session selection

### 14. `NOOK_ZYGOTE_MONITOR_READY` is only a weak local readiness signal

During the `zygote-control` timing review, an important boundary was clarified:

- `ninjector::IsZygoteMonitorReady()` only checks remote process env state
  `NOOK_ZYGOTE_MONITOR_READY=1`
- that means the zygote-side hook install path completed locally
- it does **not** guarantee that server-side authoritative control readiness exists yet

Authoritative RPC readiness remains:

- control-stage `AGENT_READY` observed by the server
- followed by ready-gated session lookup in `zygote_control_rpc`

This distinction matters because otherwise device-side logs can falsely suggest
"zygote monitor is ready" while RPC still races or times out waiting for the authoritative control channel.

### 15. Authoritative agent readiness is now an explicit registry state

The earlier ready-gated RPC fix still used cached `AGENT_READY` frames as a proxy for authoritative readiness.
That worked, but mixed two separate concerns:

- whether a session is authoritative and ready for control RPC
- whether a runtime-stage `AGENT_READY` frame should be cached/replayed to the host

This was tightened by:

- adding explicit authoritative-ready tracking in
  [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
  and
  [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- marking control-stage and runtime-stage readiness explicitly from
  [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- keeping runtime `AGENT_READY` frame caching separate from authoritative control readiness

Result:

- control-stage `AGENT_READY` can now establish authoritative control readiness
  without pretending to be a runtime-stage cached ready frame
- `zygote-control` RPC readiness now has an explicit state model in the registry
- later device-side timing work can inspect "authoritative ready vs runtime ready"
  as distinct phases instead of a single overloaded signal

### 16. Control-stage `AGENT_READY` is now emitted at local zygote-control readiness time

Another device-side timing window was identified:

- `NookZygoteMonitorInitialize()` completed hook install and internal RPC registration
- but authoritative control readiness was only notified later when
  `NookAgentInitializeForZygoteControl()` returned to its post-monitor path

This left a local-ready to server-ready gap that could still inflate `ready wait`
or make the server observe readiness later than necessary.

This was tightened by:

- adding `NotifyZygoteControlReadyToServer()` in
  [src/framework/NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
- invoking it directly from
  [src/framework/nook_zygote_control.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/nook_zygote_control.cpp)
  immediately after local zygote-control readiness is established
- reducing `NookAgentInitializeForZygoteControl()` to reuse the same notify helper instead of
  duplicating its own send path

Result:

- control-stage authoritative readiness is now emitted at the earliest local-ready point
- the remaining timing question shifts from "why is ready sent late?" to
  "does the server observe the earlier control-stage ready reliably on device?"
- the next rational step is device verification, not more host-side abstraction work

## Current Ownership Model

The code now behaves according to the following explicit rules:

1. only one active spawn owner may exist at a time
2. only the matching owner may detach that owner state
3. foreign finalize requests must not clear another owner's state
4. failed respawn attempts must not erase an earlier successful owner
5. successful spawn paths commit active owner state at a single top-level point
6. terminal spawn exit is rendered from one explicit `SpawnOutcome`
7. terminal spawn classification is performed by one helper before rendering
8. zygote-control fallback eligibility is decided by one helper before backend fallthrough
9. successful spawn ownership commit is performed by one helper before return

That is the current stable contract.

## Regression Coverage Added Or Updated

The communication test suite now covers:

- foreign finalize does not steal `zygote-control` teardown
- foreign finalize does not clear legacy-owned state
- failed respawn does not erase existing legacy owner state
- duplicate same-identifier spawn is rejected and preserves owner state
- different-identifier spawn is rejected while a single active owner exists

Main test file:

- [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

## Local Verification Used During This Convergence Step

Fresh binaries that passed during this phase:

- `build/test_ninjector_spawn_owner_record_green.exe`
- `build/test_ninjector_spawn_outcome_green.exe`
- `build/test_ninjector_duplicate_owner_green_fresh.exe`
- `build/test_ninjector_single_slot_owner_green_fresh.exe`
- `build/test_ninjector_pending_commit_fresh.exe`
- `build/test_ninjector_spawn_outcome_refactor_fresh.exe`
- `build/test_ninjector_spawn_outcome_regressions_green_v3.exe`
- `build/test_ninjector_spawn_outcome_regressions_green_v4.exe`
- `build/test_ninjector_spawn_outcome_regressions_green_v5.exe`
- `build/test_ninjector_spawn_outcome_regressions_green_v6.exe`
- `build/test_ninjector_spawn_outcome_terminal_finalize_fresh.exe`
- `build/test_ninjector_spawn_outcome_classify_fresh.exe`
- `build/test_ninjector_zygote_fallback_classify_fresh.exe`
- `build/test_ninjector_spawn_success_commit_fresh.exe`
- `build/test_server_zygote_control_rpc_regressions_v2.exe`
- `build/test_zygote_control_rpc_dispatch_refactor.exe`
- `build/test_session_registry_ready_gate.exe`
- `build/test_session_registry_authoritative_ready.exe`
- `build/test_zygote_control_rpc_ready_gate.exe`
- `build/test_zygote_control_rpc_authoritative_ready.exe`
- `build/test_server_zygote_control_rpc_regressions_v3.exe`
- `build/test_server_zygote_control_rpc_regressions_v4.exe`
- `build/test_server_handlers_control_ready_authoritative.exe`
- `build/test_zygote_control_regressions_v2.exe`
- `build/test_server_zygote_control_rpc_regressions_v5.exe`
- `build/test_zygote_control_rpc_authoritative_ready_v2.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v3.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v4.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v5.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v6.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v7.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v8.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v9.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v10.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v14.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v15.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v16.exe`
- `build/test_ninjector_zygote_lifecycle_state_regressions_green_v17.exe`

## Current Position

At this point the spawn/finalize surface is materially cleaner:

- finalize is owner driven
- active owner is explicit and singular
- success commit is centralized
- local spawn result state is partially grouped

The next logical step is not more boundary patching. It is to complete the `SpawnOutcome` direction by moving route/result classification into explicit fields, so `Spawn()` becomes:

- compute route result
- decide fallback / abort
- commit owner state once
- emit terminal outcome from one object

That would be the first point where `Spawn()` reads like an actual state machine instead of a long procedural branch chain.
