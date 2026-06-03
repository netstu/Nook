# zygote-control success attempt bridge

## Context

Before this change, zygote-control had become asymmetric:

- failed attempt -> outcome already had a dedicated bridge helper
- successful attempt -> commit still flowed through caller-side field assembly in `Spawn()`

That left one more manual seam in the zygote-control branch of `Spawn()`.

## Change

Added a success-side bridge helper:

- `ApplySuccessfulZygoteControlAttemptResult(...)`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Behavior:

- validates `attempt.success`
- propagates `attempt.pid`
- reuses `CommitSuccessfulSpawnOutcome(...)`
- commits zygote-control owner state using:
  - `attempt.owned_transaction.identifier`
  - `attempt.owned_transaction`
  - caller-provided `SpawnOwnedState`

`Spawn()` no longer manually copies zygote-control success fields before committing owner state.

At this point zygote-control now has symmetric bridges:

- `TrySpawnViaZygoteControl(...)` returns `ZygoteControlAttemptResult`
- failure path: `ApplyFailedZygoteControlAttemptResult(...)`
- success path: `ApplySuccessfulZygoteControlAttemptResult(...)`

## Tests

Added white-box regression in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplySuccessfulZygoteControlAttemptResultSeedsCommit()`

It verifies:

- pid propagation
- `final_status == kSuccess`
- pending commit contains zygote-control transaction
- committed owner state backend is `kZygoteControl`
- transaction payload preserves token and armed targets

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_success_bridge_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_success_bridge_green.exe"
```

## Why this matters

This removes the last direct zygote-control success-field assembly from `Spawn()`.

What remains in `Spawn()` for zygote-control is now mostly top-level route selection and terminal decision control, which is a much cleaner boundary for the next step: pulling backend-routing decisions toward a more explicit spawn state machine on the way to real `agent-owned stable spawn`.
