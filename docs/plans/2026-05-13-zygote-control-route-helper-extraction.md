# zygote-control route helper extraction

## Context

After success/failure attempt bridges were introduced, `Spawn()` still directly owned the whole zygote-control route block:

- set `route_attempt`
- handle success attempt
- handle failed attempt
- format abort logging
- format fallback logging

The data assembly had already been reduced, but the route block itself was still embedded in `Spawn()`.

## Change

Extracted that route block into:

- `ApplyZygoteControlRouteAttempt(...)`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

The helper now owns:

- setting `outcome.route_attempt = kZygoteControl`
- success bridge via `ApplySuccessfulZygoteControlAttemptResult(...)`
- failed-attempt bridge via `ApplyFailedZygoteControlAttemptResult(...)`
- abort log formatting
- fallback log formatting
- final error formatting for the strict-abort case

`Spawn()` now only:

1. obtains `ZygoteControlAttemptResult`
2. delegates to `ApplyZygoteControlRouteAttempt(...)`
3. returns immediately on success
4. continues to symbi/legacy only when the helper allowed fallback

## Tests

Added white-box coverage in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplyZygoteControlRouteSuccessCommitsOutcome()`
- `TestApplyZygoteControlRouteAbortsStrictFailure()`

These verify the helper can independently drive:

- zygote-control success commit
- strict zygote-control abort formatting

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_route_bridge_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_route_bridge_green.exe"
```

## Why this matters

This is the first extraction that meaningfully reduces `Spawn()` as a control-flow owner, not just a data assembler.

At this point the zygote-control branch is largely a single route helper. What remains in `Spawn()` is mostly top-level backend orchestration across:

- zygote-control
- symbi
- legacy ncore

That is the right remaining shell to converge before introducing a broader spawn state machine for real `agent-owned stable spawn`.
