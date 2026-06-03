# 2026-05-15 Agent-Owned Stable Spawn: Remove Routing Snapshot Owner Mirror

## Context

After removing committed owner mirror state, the routing snapshot layer still carried another ownership duplicate:

- `SpawnExecutionState::ownership_state`
- `SpawnRoutingSnapshot::ownership_state`

The snapshot version existed only to feed `ApplySpawnRoutingSnapshot(...)` and validate committed route transitions, which meant the snapshot API was still wider than the routing facts it actually needed to express.

## Change

Removed ownership fields from `SpawnRoutingSnapshot`:

- removed `update_ownership_state`
- removed `ownership_state`

Moved ownership transition validation fully into:

- `TransitionSpawnOwnershipState(...)`

Updated `ApplySpawnRoutingSnapshot(...)` so it now validates committed routing states against the already-applied execution ownership state instead of an ownership mirror carried inside the snapshot.

## Result

Routing snapshots now describe only routing facts:

- routing state
- routing progress
- current route step
- zygote-control route state
- route windows

Ownership remains part of `SpawnExecutionState`, but it is no longer duplicated into routing snapshots.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

No `update_ownership_state` / `snapshot.ownership_state` references remain.

## Notes

`SpawnExecutionState::ownership_state` remains intentionally.

At this point it is the execution state machine's authoritative ownership state, not a snapshot mirror.
