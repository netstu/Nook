# Zygote-Control Route State Deferred/Aborted Validation

## Context

`SpawnExecutionState` had already gained an explicit `zygote_control_route_state`, but
`ApplySpawnRoutingSnapshot()` only constrained:

- `kEntered`
- `kCommitted`
- `kSkipped`

The remaining route-local outcomes:

- `kDeferredToFallback`
- `kAborted`

were still accepted as long as the transition was not blocked by the coarse
`kNotStarted -> { kNotStarted, kSkipped, kEntered }` rule.

That left a gap where the host-side snapshot helper could accept these route-local
states even when the outer routing context no longer matched an active
zygote-control attempt.

## Change

Added two white-box tests covering invalid zygote-control route-state snapshots:

- reject `kDeferredToFallback` when the routing context is no longer the active
  zygote-control route
- reject `kAborted` when the route step has moved away from zygote-control

Then tightened `ApplySpawnRoutingSnapshot()` so both `kDeferredToFallback` and
`kAborted` now require alignment with the live zygote-control route context:

- outer `routing_state == kRunning`
- current route step is `kZygoteControl`
- zygote-control window is `kEntered`

This matches the real write sites in `ApplySpawnRoutingAttempts()`:

- `kAborted` is written only when `ApplyZygoteControlRouteAttempt()` fails after
  the zygote-control route has already been entered
- `kDeferredToFallback` is written only after a non-success zygote-control
  attempt while still inside the zygote-control route window and before outer
  routing advances to later backends

## Why It Matters

This further narrows the host-side routing model so route-local zygote-control
states cannot be replayed or synthesized out of context by later helper calls.

That is a prerequisite for the next stage of boundary convergence:

- the outer host routing state becomes the single authoritative model for
  zygote-control route entry, deferral, and abort semantics
- later `zygote-control` refactors can rely on this state model without
  re-deriving intent from scattered helper-local side effects

## Verification

Host-side compile and test:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_zygote_route_deferred_abort_green7.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_zygote_route_deferred_abort_green7.exe"
```
