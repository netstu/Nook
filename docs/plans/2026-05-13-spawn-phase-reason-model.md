# Spawn phase reason model

## Context

`SpawnExecutionPhase` already made the outer `Spawn()` orchestration observable as a small phase machine, but it still only answered "where are we now".

For the next convergence steps, especially:

- tighter zygote-control route boundaries
- explicit fallback ownership
- later agent-owned stable spawn

the host-side state also needs to answer "why did we move here".

## Change

Added `SpawnExecutionReason` and stored it in `SpawnExecutionState` as `phase_reason`.

The current reason surface is intentionally small and tied to the existing phase model:

- `kInitialized`
- `kBeginRouting`
- `kRouteCommittedFromZygoteControl`
- `kRouteCommittedFromSymbi`
- `kRouteCommittedFromLegacy`
- `kRouteDeferredForTerminalClassification`
- `kBeginTerminalClassification`
- `kTerminalOutcomeResolved`
- `kTerminalOutcomeFinalized`
- `kCompletedAfterCommittedRoute`
- `kCompletedAfterTerminalOutcome`

`TransitionSpawnExecutionPhase(...)` now updates both:

- `phase`
- `phase_reason`

## Why this matters

This keeps the state model useful without pushing logging policy into it yet.

The next refactors can branch on explicit state semantics instead of re-deriving intent from call sites. That is the bridge from "helper extraction" into a real outer execution model.

In practice this means:

- route commit now records which backend won
- deferred terminal classification is no longer just a generic phase move
- completed state distinguishes fast-success return from terminally classified return

## Tests

Added and updated white-box coverage for:

- initial state seeding of `phase_reason`
- legal phase transitions carrying the expected reason
- illegal transition rejection with explicit reason input

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_reason_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_reason_green.exe"
```

## Next

The next useful step is to stop treating routing as one large helper and model route-attempt progression explicitly in the execution state, so zygote-control and fallback ownership can be expressed as outer-state transitions instead of implicit helper-local behavior.
