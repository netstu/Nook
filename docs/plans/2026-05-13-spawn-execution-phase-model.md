# spawn execution phase model

## Context

After introducing `SpawnExecutionState`, the outer shell finally had a runtime-state carrier, but it still lacked explicit phase tracking.

That meant the code had a state object, but not yet a visible notion of “where in the state machine this execution is.”

## Change

Introduced:

- `SpawnExecutionPhase`

Current phases:

- `kInit`
- `kRouting`
- `kTerminal`
- `kCompleted`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Phase transitions currently wired:

- `BuildSpawnExecutionState(...)` seeds `kInit`
- `ApplySpawnRoutingAttempts(...)` enters `kRouting`
- successful routing completion moves to `kCompleted`
- `ApplyTerminalSpawnOutcome(...)` enters `kTerminal`
- terminal finalize completion moves to `kCompleted`

This is intentionally minimal; the point is to make phase explicit before growing more transitions.

## Tests

Updated/added white-box coverage in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestBuildSpawnExecutionStateCarriesPolicyAndAttempt()`
  - now verifies `phase == kInit`
- `TestApplySpawnRoutingSucceedsOnZygoteControlSuccess()`
  - now verifies successful routing ends at `kCompleted`
- `TestApplyTerminalSpawnOutcomeClassifiesThenFinalizes()`
  - now verifies terminal resolution ends at `kCompleted`

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_phase_model_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_phase_model_green.exe"
```

## Why this matters

This is the first time the outer spawn shell has an explicit phase model rather than only implicit control flow.

That means the next step no longer needs to be “invent a state machine.” The state machine has started. The remaining work is to make more transitions explicit and move branch behavior under those transitions.

That is exactly the right staging point before pushing this outer shell into the real `agent-owned stable spawn` state model.
