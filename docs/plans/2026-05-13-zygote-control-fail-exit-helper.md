## Summary

This checkpoint starts unifying `TrySpawnViaZygoteControl()` failure exits behind a shared helper instead of repeating:

- snapshot transaction state
- set error
- return false

## What Changed

- Added `FailZygoteControlSpawn(...)` in [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp) and declared it in [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h).
- The helper:
  - snapshots current recorder state into the transaction
  - optionally copies targets
  - sets `error_message`
  - returns `false`
- Replaced several direct early-failure exits in `TrySpawnViaZygoteControl()` with this helper, including:
  - source process not found
  - no targets armed
  - launch failure
  - required-target inject failure
  - required-target arm failure
  - required-target install failure

## Why

Previous cleanup extracted transaction-state snapshotting, but every failure branch still had to repeat the last two steps:

- `SetError(...)`
- `return false`

This helper reduces that repetition and makes it easier to continue collapsing spawn failure handling into a more structured result path.

## Tests

Added regression in [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestFailZygoteControlSpawnSnapshotsTransactionAndError`

Verification:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_fail_helper_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_fail_helper_green.exe"
```

## Progress Impact

This is still not the final structured result model, but it is the first step toward removing the many ad-hoc early returns in `TrySpawnViaZygoteControl()`.

The next step is to continue collapsing the remaining failure exits and make spawn failure classification depend less on scattered local strings and more on one structured outcome/transaction object.
