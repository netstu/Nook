# zygote-control attempt result structuring

## Context

After converging failed-outcome assembly, `TrySpawnViaZygoteControl(...)` was still exposing the old surface:

- `bool` return
- `int* pid`
- `ZygoteControlOwnedTransaction*`
- `std::string* error_message`

That forced `Spawn()` to keep doing manual output reassembly even though the inner zygote-control path already conceptually had a single attempt result.

## Change

Converted `TrySpawnViaZygoteControl(...)` to return a structured result object:

- `success`
- `pid`
- `owned_transaction`
- `error_message`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

New type:

- `ZygoteControlAttemptResult`

Behavior notes:

- failure exits now populate `result.error_message` through the existing `FailZygoteControlSpawn(...)` helper
- transaction snapshots remain authoritative and are carried in `result.owned_transaction`
- success path now fills `result.success`, `result.pid`, and the committed transaction snapshot
- `Spawn()` consumes the attempt result directly and only copies:
  - `pending_zygote_transaction`
  - `outcome.zygote_control_error`
  - `pid`

This removes another out-parameter-heavy boundary and makes the zygote-control path closer to a real transaction/attempt model.

## Tests

Added white-box regression in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestTrySpawnViaZygoteControlReturnsStructuredFailureResult()`

It verifies that a launch failure now returns a fully structured attempt result with:

- `success == false`
- `pid == 0`
- `error_message == "start_target_app failed"`
- transaction identifier preserved
- failure/lifecycle state snapshot preserved
- armed target snapshot preserved

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_attempt_result_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_attempt_result_green.exe"
```

## Next

The next convergence step should be to push more route/backend hinting into the attempt result itself, so `Spawn()` no longer has to infer as much from `outcome` plus fallback policy.

That is the last meaningful cleanup layer before moving from zygote-control prework into the real `agent-owned stable spawn` state model.
