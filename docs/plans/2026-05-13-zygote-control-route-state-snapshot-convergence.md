# Zygote-control route state snapshot convergence

## Context

`SpawnZygoteControlRouteState` had already been introduced as part of the outer host-side routing model.

But its writes still lived outside the routing snapshot helper, which meant:

- route-state writes were converged for the generic outer routing surface
- while zygote-control route-state writes still had special-case direct assignments

That was inconsistent with the direction of the current cleanup track.

## Change

Extended `SpawnRoutingSnapshot` and `ApplySpawnRoutingSnapshot(...)` to include:

- `update_zygote_control_route_state`
- `zygote_control_route_state`

Then replaced the direct zygote-control route-state assignments in `ApplySpawnRoutingAttempts(...)` with snapshot-based writes for:

- `kEntered`
- `kCommitted`
- `kDeferredToFallback`
- `kSkipped`

The immediate abort path now also uses the same write surface before returning.

## Why this matters

This is a small but important convergence step:

- the generic routing surface already had a single write path
- now the explicit zygote-control route-state surface also participates in that path

That makes the outer host-side model more internally consistent and reduces the amount of route-boundary state that still bypasses the snapshot/update helper.

## Verification

No intended behavior change.

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_zygote_route_snapshot_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_zygote_route_snapshot_green.exe"
```

## Next

The next useful step is to start validating `zygote_control_route_state` transitions inside the snapshot helper itself, so this route-specific surface is not only converged but also constrained by the same host-side legality model as the rest of routing state.
