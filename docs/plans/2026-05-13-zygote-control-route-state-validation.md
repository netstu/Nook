# Zygote-control route state validation

## Context

`SpawnZygoteControlRouteState` had already been converged into the routing snapshot write surface.

That was structurally useful, but the helper still treated this route-specific state as an unconstrained write target.

The first obvious problem was that the host-side state could jump directly from:

- `kNotStarted`

to:

- `kCommitted`
- `kDeferredToFallback`
- `kAborted`

without ever recording that the zygote-control route had been entered or skipped.

## Change

Added the first legality rule for `zygote_control_route_state` inside `ApplySpawnRoutingSnapshot(...)`.

From `kNotStarted`, the helper now only allows:

- `kNotStarted`
- `kSkipped`
- `kEntered`

Direct jumps from `kNotStarted` to:

- `kCommitted`
- `kDeferredToFallback`
- `kAborted`

are rejected with:

- `false`
- `error_message = "invalid spawn routing snapshot transition"`

## Why this matters

This is intentionally just the first constraint, not the full zygote-control route state machine.

But it is enough to start making the route-specific host-side state behave like a real state surface instead of a free-form status label.

That is important before more zygote-control semantics are moved out of helper-local flow and into explicit host-side transitions.

## Tests

Added white-box coverage for rejecting:

- `kNotStarted -> kCommitted`
- `kNotStarted -> kDeferredToFallback`

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_zygote_route_validation_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_zygote_route_validation_green.exe"
```

## Next

The next useful step is to add the second layer of zygote-control route-state validation:

- `Entered -> Committed`
- `Entered -> DeferredToFallback`
- `Entered -> Aborted`

with alignment to the outer routing state where appropriate.
