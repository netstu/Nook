## Summary

This checkpoint makes spawn terminal classification prefer structured failure transaction state from `SpawnOutcome` instead of depending only on `zygote_control_state` or detail-string inference.

## What Changed

- Added `ResolveOutcomeZygoteControlState(...)` in [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp) and declared it in [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h).
- Resolution order is now:
  1. `outcome.zygote_control_state`
  2. `outcome.failed_zygote_control_transaction`
  3. detail-string inference
- `FinalizeSpawnOutcome()` now uses `ResolveOutcomeZygoteControlState(...)`.
- `ShouldAllowZygoteControlFallback()` now also prefers `failed_zygote_control_transaction` when `zygote_control_state` is absent.

## Why

After the previous step, `SpawnOutcome` could carry a structured failed zygote-control transaction snapshot, but terminal formatting and fallback classification were still not consuming it directly.

That left part of the failure-path structuring work unused.

This change closes that loop:

- failure snapshots are now not just captured
- they are also consumed by terminal decision points

## Tests

Added regressions in [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestSpawnOutcomeFallbackClassificationPrefersFailedTransactionState`
- `TestSpawnOutcomeAbortFormatsFailedTransactionStateInsteadOfDetailFallback`

Verification:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_outcome_state_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_outcome_state_green.exe"
```

## Progress Impact

This is still transition work, but now the state pipeline is more coherent:

- spawn failure path can snapshot transaction state
- terminal decision points can consume that snapshot
- finalize can write teardown state back into transaction state

The next serious step is to reduce the remaining duplicated recorder-to-transaction copying and move failure classification closer to one explicit spawn/result transaction model.
