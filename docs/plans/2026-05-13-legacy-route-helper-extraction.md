# legacy route helper extraction

## Context

After zygote-control and symbi route helpers were extracted, `Spawn()` still directly owned the legacy backend block:

- commit legacy success
- set fallback policy on legacy success
- capture legacy probe failure when legacy fallback is disabled

That was the last backend-specific route block still inlined in `Spawn()`.

## Change

Extracted the legacy backend route handling into:

- `ApplyLegacyRouteResult(...)`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Helper responsibilities:

- set `outcome.route_attempt = kLegacy`
- on legacy success:
  - derive `fallback_policy` (`kNotNeeded` vs `kAllowed`)
  - commit owner state through `CommitSuccessfulSpawnOutcome(...)`
- on legacy failure/probe:
  - preserve `outcome.legacy_error`

`Spawn()` now delegates the full legacy route result handling to this helper.

## Tests

Added white-box regressions in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplyLegacyRouteSuccessCommitsOutcome()`
- `TestApplyLegacyRouteFailureCapturesErrorWhenProbeOnly()`

These verify:

- legacy success commit is now helper-owned
- legacy probe/error capture is preserved without requiring immediate terminal handling

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_legacy_route_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_legacy_route_green.exe"
```

## Why this matters

With zygote-control, symbi, and legacy all moved behind route helpers, `Spawn()` is now mostly a backend orchestrator shell plus shared terminal classification.

That is the right point to start the next step:

- either extract the shared top-level orchestration into an explicit routing state machine
- or begin mapping this orchestration shell directly onto the `agent-owned stable spawn` state model

Either way, the backend-specific inline control flow has now been largely removed.
