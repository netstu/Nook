# spawn routing orchestration helper

## Context

After backend-specific route helpers were extracted, `Spawn()` still directly owned the sequential orchestration:

- maybe run zygote-control route
- maybe run symbi route
- maybe run legacy route
- exit early on backend success
- otherwise leave accumulated outcome state for later terminal classification

At that point the function was no longer backend-detail-heavy, but it still encoded the orchestration shell inline.

## Change

Extracted this orchestration shell into:

- `ApplySpawnRoutingAttempts(...)`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

The helper now owns:

- zygote-control route helper dispatch
- symbi route helper dispatch
- legacy route helper dispatch
- early success returns
- accumulation of route-level outcome state for later terminal classification

`Spawn()` now:

1. validates request
2. acquires any precomputed zygote-control attempt
3. delegates route orchestration to `ApplySpawnRoutingAttempts(...)`
4. if routing already committed success, returns
5. otherwise performs the remaining shared terminal classification/finalization

## Tests

Added white-box regressions in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplySpawnRoutingSucceedsOnZygoteControlSuccess()`
- `TestApplySpawnRoutingAttemptsCanDeferToClassification()`

These verify:

- orchestration helper can terminate early on backend success
- orchestration helper can also finish with accumulated route errors and leave terminal resolution to the shared final stage

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_orchestration_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_orchestration_green.exe"
```

## Why this matters

This is the main structural milestone in the current cleanup track.

At this point `Spawn()` is mostly reduced to:

- request validation / owner guard
- one orchestration helper call
- one shared terminal-classification/finalization tail

That is close to the boundary where the next change should stop being “extract another helper” and start becoming “replace the remaining tail with an explicit routing/terminal state model,” which is the natural lead-in to real `agent-owned stable spawn`.
