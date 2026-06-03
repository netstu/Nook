# zygote-control attempt to outcome bridge

## Context

After introducing `ZygoteControlAttemptResult`, `Spawn()` still had a small amount of manual zygote-control failure shaping:

- copy `attempt.owned_transaction` into a local transaction
- copy `attempt.error_message` into `outcome.zygote_control_error`
- then call `ApplyFailedZygoteControlOutcome(...)`

That was smaller than the old out-parameter path, but it still split the attempt-to-outcome bridge between caller and helper.

## Change

Added a dedicated bridge helper:

- `ApplyFailedZygoteControlAttemptResult(...)`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Behavior:

- copies `attempt.error_message` into `outcome.zygote_control_error`
- snapshots `attempt.owned_transaction`
- delegates fallback-policy/final-status/state shaping to the existing
  `ApplyFailedZygoteControlOutcome(...)`

`Spawn()` no longer manually reassembles the failed zygote-control attempt before asking for fallback policy.

Success-path handling still explicitly moves:

- `attempt.pid`
- `attempt.owned_transaction`

That separation is intentional: success still feeds the active owner-state commit path, while failure now has a dedicated outcome bridge.

## Tests

Added white-box regression in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplyFailedZygoteControlAttemptResultSeedsOutcome()`

It verifies a failed attempt now seeds:

- `outcome.zygote_control_error`
- `fallback_policy`
- `final_status`
- failed transaction snapshot
- resolved `zygote_control_state`

Also kept coverage on the structured attempt result itself:

- `TestTrySpawnViaZygoteControlReturnsStructuredFailureResult()`

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_attempt_to_outcome_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_attempt_to_outcome_green.exe"
```

## Why this matters

This change removes another caller-side assembly seam from the zygote-control path.

At this point the remaining gap before real `agent-owned stable spawn` is no longer “too many scattered scalar outputs”; it is mainly that `Spawn()` still owns the top-level backend routing and terminal result classification. That is a cleaner boundary to attack next.
