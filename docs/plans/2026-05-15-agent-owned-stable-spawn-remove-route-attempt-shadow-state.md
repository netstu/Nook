# 2026-05-15 Agent-Owned Stable Spawn: Remove Route Attempt Shadow State

## Context

`SpawnOutcome::route_attempt` was no longer used by runtime decision-making.

After the earlier ownership/finalize convergence work, this field had become a pure record-only state:

- written in route-result helpers
- asserted in tests
- never read by the actual spawn/finalize runtime path

That made it another unnecessary state surface inside the spawn outcome record.

## Change

Removed `SpawnOutcome::route_attempt`.

Updated:

- `ApplySuccessfulRouteCommit(...)`
- `ApplyZygoteControlRouteAttempt(...)`
- `ApplySymbiRouteResult(...)`
- `ApplyLegacyRouteResult(...)`

Tests were updated to validate real outcome facts instead:

- `final_status`
- committed backend in `pending_commit.spawn_state.backend`
- concrete error fields like `symbi_error` / `legacy_error`

## Result

`SpawnOutcome` now carries only runtime-relevant result state instead of an extra record-only route label.

This further reduces duplicated or non-authoritative outcome state on the path toward agent-owned stable spawn.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

No `SpawnOutcome::route_attempt` references remain.
