# symbi route helper extraction

## Context

After zygote-control route extraction, `Spawn()` still directly owned the symbi route block:

- choose route attempt tag
- commit success into owner state
- preserve failure detail for later legacy fallback / terminal classification

That meant the top-level spawn shell was still partially backend-specific.

## Change

Extracted the symbi route block into:

- `ApplySymbiRouteResult(...)`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Helper responsibilities:

- set `outcome.route_attempt` to:
  - `kExplicitSymbi` when explicit symbi was requested
  - `kSymbi` otherwise
- on success:
  - commit owner state through `CommitSuccessfulSpawnOutcome(...)`
- on failure:
  - preserve `outcome.symbi_error`
  - leave terminal classification to the outer result model

`Spawn()` now delegates the entire symbi route result handling to this helper.

## Tests

Added white-box regressions in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplySymbiRouteSuccessCommitsOutcome()`
- `TestApplySymbiRouteFailureAllowsFallback()`

These verify that the helper independently handles:

- symbi success commit
- symbi failure state capture without prematurely terminating the outer flow

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_symbi_route_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_symbi_route_green.exe"
```

## Why this matters

With both zygote-control and symbi route blocks extracted, `Spawn()` is much closer to a pure backend orchestrator:

- probe zygote-control route helper
- probe symbi route helper
- probe legacy route
- run shared terminal classification

The next natural step is to do the same for legacy route ownership and then revisit whether the remaining outer shell should become an explicit spawn routing state machine for `agent-owned stable spawn`.
