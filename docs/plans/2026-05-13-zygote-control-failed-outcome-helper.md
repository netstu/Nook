## Summary

This checkpoint begins structuring zygote-control spawn failure result assembly by moving repeated `SpawnOutcome` failure seeding into a helper.

## What Changed

- Added `ApplyFailedZygoteControlOutcome(...)` in [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp) and declared it in [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h).
- The helper currently seeds:
  - `fallback_policy`
  - `final_status`
  - failed transaction snapshot
  - derived `zygote_control_state` via the existing snapshot flow
- In `Spawn()`, the two zygote-control failure branches now call this helper instead of manually assigning those fields.

## Why

Before this step, even after introducing structured failed transaction snapshots, spawn-side zygote-control failure handling still hand-built the failed `SpawnOutcome` in multiple places.

That was the next obvious duplication point after unifying the lower-level fail exits.

This helper is a transitional step toward a fuller structured failure-result builder.

## Tests

Added regression in [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplyFailedZygoteControlOutcomeSeedsStateAndTransaction`

Verification:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_failed_outcome_helper_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_failed_outcome_helper_green.exe"
```

## Progress Impact

This is still an intermediate step, but it matters because:

- fail exits are becoming unified
- failed transaction snapshots are being consumed consistently
- spawn-side failed outcome assembly is no longer handwritten in multiple branches

The next logical step is to expand this helper or replace it with a fuller result-construction path so that zygote-control failure handling becomes one structured flow end-to-end.
