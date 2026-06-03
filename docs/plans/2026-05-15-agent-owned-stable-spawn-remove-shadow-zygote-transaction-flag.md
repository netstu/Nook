# 2026-05-15 Agent-Owned Stable Spawn: Remove Shadow Zygote Transaction Flag

## Context

`server/ninjector_spawn_injector` still carried duplicated zygote-control transaction state:

- `ActiveSpawnOwner::zygote_control_transaction`
- `ActiveSpawnOwner::has_zygote_control_transaction`

That boolean was a pure shadow flag. It duplicated information already present in the transaction payload and widened the state surface inside the experimental `zygote-control` preparation work.

For the current phase, the goal is still:

- keep default real-device spawn on the stable path
- continue converging experimental `zygote-control` state toward agent-owned stable spawn
- reduce duplicated ownership / transaction state before changing backend authority

## Change

Removed `has_zygote_control_transaction` from `ActiveSpawnOwner` / `PendingSpawnCommit` usage and replaced checks with transaction-content inference.

Added:

- `NinjectorSpawnInjector::HasZygoteControlTransactionRecord(const ZygoteControlOwnedTransaction&)`

Updated code paths:

- `TakeActiveOwnerForFinalize(...)`
- `TakeResidualZygoteControlTransactionForFinalize(...)`
- `BuildPendingSpawnCommit(...)`

Updated tests so they now assert on transaction content instead of the removed shadow boolean.

## Why This Matters

This narrows one more internal state surface in the `zygote-control` pipeline:

- fewer mirrored flags that can drift from payload state
- finalize / residual transaction handling now depends on the transaction record itself
- easier next-step convergence for remaining outcome-side duplicated flags

This is preparatory work for `agent-owned stable spawn`; it does not change the default backend or runtime deployment shape.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
build\test_session_registry_identity_v2.exe
build\test_zygote_control_rpc_identity_v2.exe
```

All passed.

## Next

Next safe convergence candidate:

- `SpawnOutcome::has_failed_zygote_control_transaction`

The default stable spawn path remains unchanged.
