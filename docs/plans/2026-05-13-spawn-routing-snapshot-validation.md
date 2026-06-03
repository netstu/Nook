# Spawn routing snapshot validation

## Context

`ApplySpawnRoutingSnapshot(...)` had already converged outer routing state writes behind one helper.

That was useful structurally, but it was still a raw write surface:

- any caller could write any routing state
- there was no protection against obviously impossible jumps

The next convergence step needed at least a minimal guardrail before deeper zygote-control boundary work starts to rely on this surface.

## Change

Upgraded `ApplySpawnRoutingSnapshot(...)` from a `void` write helper into a validated `bool` helper with `error_message`.

Current validation is intentionally narrow:

- from `routing_state = kNotStarted`, the helper only allows staying at `kNotStarted` or moving to `kRunning`
- direct jumps from `kNotStarted` to committed or deferred routing states are rejected

Rejected writes return:

- `false`
- `error_message = "invalid spawn routing snapshot transition"`

## Why this matters

This is not the full routing state machine yet.

It is the first step from:

- "single write API"

to:

- "single write API with minimal legality checks"

That makes the outer route-state surface safer to build on without overcommitting to a large validation matrix too early.

## Tests

Added white-box coverage for:

- rejecting a direct `kNotStarted -> kCommittedFromLegacy` snapshot write

Existing routing tests remain green, confirming no behavior change to normal route orchestration.

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_routing_snapshot_validation_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_routing_snapshot_validation_green.exe"
```

## Next

The next useful step is to extend this helper from a single routing-state legality check into a small route-state transition surface that validates:

- progress order
- current route step alignment
- committed/deferred terminal routing endpoints

without yet dragging backend-internal logic into the outer model.
