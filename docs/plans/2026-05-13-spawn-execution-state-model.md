# spawn execution state model

## Context

After introducing `SpawnExecutionPolicy`, the outer shell still passed three pieces around separately:

- policy
- zygote-control attempt result
- mutable spawn outcome

That was better than loose booleans, but it was still not a single runtime-state carrier.

## Change

Introduced:

- `SpawnExecutionState`
- `BuildSpawnExecutionState(...)`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

`SpawnExecutionState` currently carries:

- `policy`
- `zygote_attempt`
- `outcome`

Integration changes:

- `ApplySpawnRoutingAttempts(...)` now consumes `SpawnExecutionState`
- `ApplyTerminalSpawnOutcome(...)` now consumes `SpawnExecutionState`
- `Spawn()` now builds one execution-state object and passes it through the outer flow

This means the remaining outer shell no longer threads runtime-state fragments independently.

## Tests

Updated orchestration/tail helper tests to use `SpawnExecutionState`.

Added white-box regression in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestBuildSpawnExecutionStateCarriesPolicyAndAttempt()`

It verifies that the builder produces a coherent runtime-state carrier even before any route is executed.

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_execution_state_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_execution_state_green.exe"
```

## Why this matters

This is the first runtime-state carrier for the outer spawn shell.

At this point the current structure is:

- `SpawnExecutionPolicy`: immutable-ish route/fallback contract
- `SpawnExecutionState`: runtime execution carrier
- route helper(s): mutate execution state
- terminal helper: resolve execution state into public outcome

That is close enough to an explicit state machine that the next step should be to stop thinking in terms of helper extraction entirely and start introducing explicit phase/state transitions for the `agent-owned stable spawn` design.
