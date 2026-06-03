# 2026-05-15 Agent-Owned Stable Spawn: Transaction-Local State Resolution

## Context

After moving spawn/finalize lifecycle writes toward `ZygoteControlOwnedTransaction`, some
resolution paths still allowed injector-global recorder state to leak back into a different
request:

- `ResolveTransactionZygoteControlState(...)` could fall through to global recorder state before
  trusting local error detail
- `FailZygoteControlSpawn(...)` still depended on recorder-backed snapshots when the transaction
  itself was empty
- `SnapshotFailedZygoteControlTransaction(...)` could leave the failed transaction without explicit
  state even when the error detail was sufficient to infer it
- `FinalizeWithoutOwnedBackend(...)` could classify finalize errors using stale recorder state when
  there was no residual zygote-control transaction for the current request

These were not just cosmetic duplicates. They were real cross-request contamination risks.

## Change

Refined state resolution so transaction-local state and local detail now win over injector-global
recorder state.

Changes:

- `ResolveTransactionZygoteControlState(...)`
  - now resolves in this order:
    1. transaction failure state
    2. transaction lifecycle state
    3. local error-detail inference
    4. recorder-backed compatibility fallback

- `FailZygoteControlSpawn(...)`
  - now infers failure/lifecycle state from the local error message when the transaction is still
    `kUnknown`

- `SnapshotFailedZygoteControlTransaction(...)`
  - now backfills the copied failed transaction from local error detail so the transaction becomes
    self-describing

- `FinalizeWithoutOwnedBackend(...)`
  - no longer uses recorder state to classify finalize errors when no residual zygote-control
    transaction participated in the current finalize path

## Result

`zygote-control` transaction handling is now materially closer to agent-owned state:

- transaction objects carry more of their own meaning
- local error detail is preferred over ambient injector state
- finalize fallback no longer inherits stale zygote recorder state from unrelated requests

Recorder state still exists, but it is increasingly compatibility-only instead of being the
authoritative source of truth.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Move from transaction-local authority toward true agent-owned stable spawn by shrinking the
remaining injector-owned zygote recorder surface and tightening owner/finalize handoff around the
committed transaction object.
