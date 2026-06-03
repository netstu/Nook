# Spawn routing progress model

## Context

The host-side outer spawn model already had:

- `phase`
- `phase_reason`
- `routing_state`
- `routing_windows`

That still left one missing dimension:

the ordered routing walk itself.

`routing_windows` answers whether each route window was entered, skipped, or only probed.
It does not answer how far the orchestration has progressed through the route order.

## Change

Added `SpawnRoutingProgress` to `SpawnExecutionState`.

Current checkpoints:

- `kNotStarted`
- `kEnteredRouting`
- `kAfterZygoteControl`
- `kAfterSymbi`
- `kAfterLegacy`

This is intentionally a coarse ordered walk, not a full attempt history.

## Current wiring

`BuildSpawnExecutionState(...)` seeds:

- `routing_progress = kNotStarted`

`ApplySpawnRoutingAttempts(...)` now advances it as the outer routing helper walks the route order:

- enter routing: `kEnteredRouting`
- finish zygote-control window: `kAfterZygoteControl`
- finish symbi window: `kAfterSymbi`
- finish legacy window: `kAfterLegacy`

That includes both:

- entered windows
- policy-skipped windows

because the goal here is to model orchestration progress, not only successful attempts.

## Why this matters

This separates two ideas that were previously implicit:

- `routing_windows`: what happened to each route window
- `routing_progress`: how far the ordered routing walk has advanced

That makes the outer host model much closer to a real routing state machine and reduces the amount of sequencing logic that still exists only as control flow inside the helper body.

## Tests

Updated white-box coverage to assert:

- initial progress state
- zygote-control success leaves progress at `kAfterZygoteControl`
- deferred routing leaves progress at `kAfterLegacy`

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_route_progress_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_route_progress_green.exe"
```

## Next

The next useful step is to add a small explicit route-step surface for the current active route, so the outer state can represent not only completed progress checkpoints but also which route step is currently being evaluated at a given moment.
