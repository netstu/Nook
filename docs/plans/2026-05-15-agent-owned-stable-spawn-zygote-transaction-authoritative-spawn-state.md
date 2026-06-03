# 2026-05-15 Agent-Owned Stable Spawn: Zygote Transaction-Authoritative Spawn State

## Context

Even after shrinking the ownership/finalize state surfaces, `zygote-control` spawn still relied on injector-global recorder state for failure/lifecycle classification:

- `current_zygote_control_lifecycle_stage_`
- `last_zygote_control_failure_state_`

`TrySpawnViaZygoteControl(...)` wrote those global fields step-by-step, and `FailZygoteControlSpawn(...)` / `SnapshotCurrentZygoteControlTransactionState(...)` then copied them back into the transaction.

That meant the transaction was not yet the authoritative source of its own spawn attempt state.

## Change

Started moving spawn-attempt authority onto `ZygoteControlOwnedTransaction` itself.

Inside `TrySpawnViaZygoteControl(...)`:

- introduced local transaction writers for:
  - `failure_state`
  - `lifecycle_state`
- updated spawn-path lifecycle/failure progression to write directly into:
  - `result.owned_transaction`

Updated:

- `SnapshotCurrentZygoteControlTransactionState(...)`

so it now preserves existing transaction state and only falls back to injector-global recorder state when the transaction fields are still `kUnknown`.

## Result

For the zygote-control spawn path:

- transaction-carried state is now authoritative when already known
- injector-global recorder state is downgraded to fallback / compatibility support

This is the first concrete step from shared recorder state toward agent-owned attempt state.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Apply the same principle to finalize-path state, so `FinalizeZygoteControlSpawn(...)` and residual finalize fallback rely less on injector-global recorder state and more on transaction-carried authority.
