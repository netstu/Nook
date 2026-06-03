# spawn phase refinement

## Context

The first `SpawnExecutionPhase` pass only modeled broad stages:

- `kInit`
- `kRouting`
- `kTerminal`
- `kCompleted`

That established phase tracking, but it still collapsed important intermediate meaning, especially the common case where routing finishes without success and defers to terminal resolution.

## Change

Refined `SpawnExecutionPhase` with additional intermediate states:

- `kRouteDeferred`
- `kTerminalResolved`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Current phase semantics:

- `kInit`
  - execution state built, no route attempted yet
- `kRouting`
  - backend routing in progress
- `kRouteDeferred`
  - routing finished without immediate success and terminal resolution is required
- `kTerminal`
  - terminal classification/finalization in progress
- `kTerminalResolved`
  - terminal result has been computed
- `kCompleted`
  - helper/function finished

Current wiring:

- routing helper now enters `kRouteDeferred` when it finishes with accumulated route-level state
- terminal helper still ends at `kCompleted`, but now explicitly steps through `kTerminalResolved` first

## Tests

Updated white-box coverage in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplySpawnRoutingAttemptsCanDeferToClassification()`
  - now asserts `phase == kRouteDeferred`

This is the first test that checks a true intermediate phase rather than only init/completed endpoints.

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_phase_refine_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_phase_refine_green.exe"
```

## Why this matters

This is the point where phases begin to express control-flow meaning rather than only lifecycle boundaries.

That matters because the upcoming `agent-owned stable spawn` work will need to reason about:

- route succeeded early
- route deferred to terminal
- terminal resolved
- owner state committed / finalized

Once those meanings exist as explicit phases, pushing the outer shell into a real state machine becomes a straightforward extension instead of a conceptual rewrite.
