# 2026-05-15 Agent-Owned Stable Spawn: Remove Owner/Backend Mirror State

## Context

`ActiveSpawnOwner` and `PendingSpawnCommit` still carried two representations of the same ownership fact:

- `spawn_state.backend`
- `ownership_state`

For committed owner records, `ownership_state` was derived from the backend through `ResolveOwnershipStateFromBackend(...)`, so this was another duplicated state surface.

## Change

Removed `ownership_state` from `ActiveSpawnOwner` / `PendingSpawnCommit`.

Updated code to derive owner classification from `spawn_state.backend` where needed:

- `TakeActiveOwnerForFinalize(...)`
- `BuildPendingSpawnCommit(...)`

Updated tests to assert on `spawn_state.backend` instead of the removed mirror field.

## Result

Committed owner records now carry one authoritative backend fact instead of two mirrored state fields.

This keeps the finalize / ownership pipeline narrower and better aligned with the direction toward agent-owned stable spawn.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Notes

This does **not** remove `SpawnExecutionState::ownership_state` or `SpawnRoutingSnapshot::ownership_state`.

Those remaining fields still belong to the execution state machine itself and are not the same kind of committed owner mirror state.
