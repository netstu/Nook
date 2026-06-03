# spawn phase semantics

## Context

The first phase refinement introduced `kRouteDeferred` and `kTerminalResolved`, but successful routing and terminal completion still collapsed too quickly into `kCompleted`.

That meant some important semantic checkpoints still were not directly observable:

- route succeeded and commit was prepared
- terminal path finished its finalization work

## Change

Refined `SpawnExecutionPhase` further with:

- `kRouteCommitted`
- `kTerminalFinalized`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Updated semantics:

- `kRouteCommitted`
  - route layer has completed successfully and owner-state commit has been prepared
- `kTerminalFinalized`
  - terminal layer has finished finalization logic and produced the public result surface

Current wiring:

- successful routing helper exits now land on `kRouteCommitted`
- terminal helper exits now land on `kTerminalFinalized`
- `Spawn()` itself still advances to `kCompleted` when the top-level function returns

This creates a clearer distinction between:

- helper-level semantic completion
- top-level function completion

## Tests

Updated white-box coverage in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplySpawnRoutingSucceedsOnZygoteControlSuccess()`
  - now asserts `phase == kRouteCommitted`
- `TestApplyTerminalSpawnOutcomeClassifiesThenFinalizes()`
  - now asserts `phase == kTerminalFinalized`

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_phase_semantics_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_phase_semantics_green.exe"
```

## Why this matters

This is the point where phase names stop being approximate flow labels and start aligning with semantic checkpoints in the spawn lifecycle.

That is exactly what the next stage of `agent-owned stable spawn` needs: explicit, observable transition points for:

- route success/deferral
- terminal resolution
- top-level completion

Once those checkpoints exist, the remaining work is mostly to formalize transitions rather than to infer them from helper nesting.
