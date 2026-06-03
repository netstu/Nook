# Spawn phase transition helper

## Context

`Spawn()` orchestration is being moved from scattered control-flow flags into an explicit execution model before implementing the real agent-owned stable spawn path.

Previous steps introduced:

- `SpawnExecutionPolicy` for route and fallback policy.
- `SpawnExecutionState` for runtime state carried through routing and terminal handling.
- `SpawnExecutionPhase` for coarse execution checkpoints.

The remaining problem was that phase changes were still direct assignments such as `state->phase = ...`. That made illegal jumps hard to detect and allowed terminal phases to be overwritten without a single transition boundary.

## Change

Added `NinjectorSpawnInjector::TransitionSpawnExecutionPhase(...)` as the only transition helper for non-initial phase changes.

Allowed transitions are now explicit:

- `kInit -> kRouting`
- `kRouting -> kRouteCommitted`
- `kRouting -> kRouteDeferred`
- `kRouteCommitted -> kCompleted`
- `kRouteDeferred -> kTerminal`
- `kTerminal -> kTerminalResolved`
- `kTerminalResolved -> kTerminalFinalized`
- `kTerminalFinalized -> kCompleted`

Current routing and terminal helpers now advance phases through this helper instead of directly assigning the phase.

## Important detail

The transition helper validates and updates phase only. It does not clear `error_message` on successful transition.

This matters because terminal failure handling writes the real spawn error before the top-level `Spawn()` moves to `kCompleted`. Clearing the error during the final transition would make failed spawn calls return `false` with an empty error string.

## Tests

Added white-box coverage for:

- legal phase progression through routing, terminal handling, and completion.
- illegal transition rejection, including `kInit -> kTerminal`.
- existing routing and terminal helper behavior with corrected phase preconditions.

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_phase_transition_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_phase_transition_green.exe"
```

## Next

The next useful step is to add transition reasons/events to `SpawnExecutionState`, so logs and tests can explain not only the current phase but why the phase changed. After that, the state model is ready for a cleaner zygote-control route boundary and the later agent-owned stable spawn implementation.
