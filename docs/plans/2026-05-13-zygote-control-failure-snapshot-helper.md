## Summary

This checkpoint reduces duplicated failure-path state copying inside `TrySpawnViaZygoteControl()` by introducing a helper that snapshots the current recorder state into the transaction object.

## What Changed

- Added `SnapshotCurrentZygoteControlTransactionState(...)` in [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp) and declared it in [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h).
- The helper copies:
  - `failure_state`
  - `lifecycle_state`
  - optional `targets`
- Replaced several repeated recorder -> transaction write-back blocks in `TrySpawnViaZygoteControl()` with this helper, including:
  - source process not found
  - required-target inject failure
  - required-target arm failure
  - required-target install failure
  - no targets armed
  - launch failure

## Why

Previous steps introduced transaction-carried failure state, but the spawn implementation still wrote that state back using repeated ad-hoc blocks.

That shape was:

- noisy
- easy to drift
- hostile to the next step of unifying failure exits

This helper does not change behavior. It just concentrates the state-copy logic into one place.

## Tests

Added regression in [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestSnapshotCurrentZygoteControlTransactionStateCopiesRecorderAndTargets`

Verification:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_snapshot_helper_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_snapshot_helper_green.exe"
```

## Progress Impact

This is still cleanup, but it is directly in service of the next step:

- unifying zygote-control failure exits
- reducing mixed string/recorder/transaction handling
- moving toward one structured failure/result path
