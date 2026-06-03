# Zygote-control route state alignment

## Context

`SpawnZygoteControlRouteState` had already been:

- introduced into `SpawnExecutionState`
- converged into the routing snapshot write surface
- given a first minimal legality rule from `kNotStarted`

That was still not enough to make it behave like a meaningful host-side route-state model.

The route-specific state could still disagree with the outer routing surface about:

- which route step was active
- whether the zygote-control window had been skipped
- whether the outer routing state had actually committed through zygote-control

## Change

Added three alignment rules inside `ApplySpawnRoutingSnapshot(...)`:

1. `SpawnZygoteControlRouteState::kEntered`

Requires:

- current route step is `kZygoteControl`

2. `SpawnZygoteControlRouteState::kCommitted`

Requires:

- outer `routing_state == kCommittedFromZygoteControl`

3. `SpawnZygoteControlRouteState::kSkipped`

Requires:

- zygote-control route window is `kSkippedByPolicy`

These checks use the snapshot-updated value when the snapshot itself carries the related field, otherwise they validate against the current state.

## Why this matters

This is the point where `SpawnZygoteControlRouteState` stops being just a readable tag and starts being aligned with the outer routing model.

The host-side state now constrains three important questions consistently:

- did zygote-control actually become the active route step
- was a zygote-control commit reflected by the outer routing state
- was a zygote-control skip reflected by the route window state

That is the first meaningful step toward making zygote-control route semantics explicit in host-side state rather than inferred from helper-local control flow.

## Tests

Added white-box coverage for rejecting:

- `kEntered` without `current_route_step = kZygoteControl`
- `kCommitted` without `routing_state = kCommittedFromZygoteControl`
- `kSkipped` without `zygote_control_window = kSkippedByPolicy`

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_zygote_route_alignment_green2.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_zygote_route_alignment_green2.exe"
```

## Next

The next useful step is to align the remaining zygote-control route endpoints:

- `kDeferredToFallback`
- `kAborted`

with outer fallback policy and route-abort semantics, so the host-side model can represent the full zygote-control route outcome surface instead of only the entry/skip/commit side.
