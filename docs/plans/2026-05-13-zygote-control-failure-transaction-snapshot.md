## Summary

This checkpoint starts structuring zygote-control failure paths by snapshotting failure-side transaction state into `SpawnOutcome` instead of leaving everything in recorder globals and error strings.

## What Changed

- Extended [SpawnOutcome](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h) with:
  - `failed_zygote_control_transaction`
  - `has_failed_zygote_control_transaction`
- Added `SnapshotFailedZygoteControlTransaction(...)` in [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp).
- `TrySpawnViaZygoteControl()` now seeds the transaction object at entry with:
  - `identifier`
  - `spawn_token`
- On several zygote-control failure exits, the current recorder snapshot is written into the transaction object before returning failure.
- In spawn abort/fallback branches, the current failed transaction snapshot is copied into `SpawnOutcome`.

## Why

Before this step:

- successful zygote-control setup could carry state in the committed transaction
- finalize teardown could write state back into that transaction
- but spawn failure paths still mostly dissolved into:
  - `error_message`
  - recorder globals
  - late string/state reconstruction

This step starts giving failure paths a structured state carrier too.

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_failure_txn_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_failure_txn_green.exe"
```

## Progress Impact

This is still transitional. Recorder state remains live and authoritative in multiple places.

What improved:

- failure-side zygote-control state now has a structured snapshot path into `SpawnOutcome`
- successful path, finalize path, and part of failure path now all have transaction-based state carriers

What remains:

- make terminal formatting and fallback classification prefer `SpawnOutcome.failed_zygote_control_transaction` directly
- reduce duplicated recorder-to-transaction copying at failure exits
- move toward one explicit spawn/result transaction model instead of mixed recorder + outcome side channels
