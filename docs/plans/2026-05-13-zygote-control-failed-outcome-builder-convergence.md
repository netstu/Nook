# zygote-control failed outcome builder convergence

## Context

`Spawn()` still had one remaining piece of ad-hoc zygote-control failure shaping:

- call `ShouldAllowZygoteControlFallback(...)`
- branch manually on abort vs fallback
- then call `ApplyFailedZygoteControlOutcome(...)` with precomputed policy/status

That left the transaction snapshot logic centralized, but the actual failed-outcome assembly still split across the caller and the helper.

## Change

Promoted `ApplyFailedZygoteControlOutcome(...)` from a field setter into the authoritative zygote-control failed-outcome builder.

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

New behavior:

- helper now snapshots failed transaction state into `SpawnOutcome`
- helper resolves fallback allowance itself using `ShouldAllowZygoteControlFallback(...)`
- helper sets:
  - `fallback_policy`
  - `final_status`
  - `zygote_control_state` via the existing transaction snapshot flow
- helper returns `bool allow_fallback`

`Spawn()` now uses one structured step:

1. `TrySpawnViaZygoteControl(...)` fails
2. `ApplyFailedZygoteControlOutcome(...)` builds the failed result
3. caller only decides whether to log abort or continue to fallback

This removes another caller-side policy split and moves zygote-control failed result ownership closer to the transaction/result model.

## Tests

Updated white-box coverage in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplyFailedZygoteControlOutcomeSeedsStateAndTransaction()`
  - now verifies strict mode returns `allow_fallback == false`
  - and produces `kForbidden + kAbort`
- `TestApplyFailedZygoteControlOutcomeAllowsFallbackWhenStateIsSoft()`
  - verifies soft failure returns `allow_fallback == true`
  - and produces `kAllowed + kUnknown`

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_failed_outcome_builder_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_failed_outcome_builder_green.exe"
```

## Next

The next useful convergence step is to stop returning bare `bool + error string` from `TrySpawnViaZygoteControl(...)` and instead return a structured attempt result carrying:

- success/failure
- owned transaction snapshot
- terminal/fallback hint
- failure detail

That would remove another large block of caller-side outcome shaping and is the natural bridge into the real `agent-owned stable spawn` transaction model.
