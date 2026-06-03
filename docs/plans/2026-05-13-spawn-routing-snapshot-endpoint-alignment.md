# Spawn routing snapshot endpoint alignment

## Context

`ApplySpawnRoutingSnapshot(...)` already validated a few obvious illegal transitions:

- `routing_state` from `kNotStarted` directly to committed/deferred
- `routing_progress` jumping too far from `kNotStarted`
- non-`kNone` route step before routing progress had begun

That still left the committed/deferred routing endpoints themselves weakly specified.

The outer routing model could still accept endpoint states that disagreed with:

- `routing_progress`
- `current_route_step`

which would undermine the purpose of having the extra route-state surface at all.

## Change

Added three cross-field endpoint alignment checks in `ApplySpawnRoutingSnapshot(...)`:

1. `CommittedFromZygoteControl`

Requires:

- `routing_progress == kAfterZygoteControl`
- `current_route_step == kZygoteControl`

2. `CommittedFromLegacy`

Requires:

- `routing_progress == kAfterLegacy`
- `current_route_step == kLegacy`

3. `DeferredToTerminal`

Requires:

- `routing_progress == kAfterLegacy`

## Why this matters

This is the first point where the helper no longer validates just single-field movement.

It now validates that the outer routing model agrees with itself about where routing stopped.

That is a real step toward turning the host-side routing surface into a coherent state model instead of a bag of readable fields.

## Tests

Added white-box coverage for rejecting:

- `CommittedFromZygoteControl` without aligned progress/step
- `CommittedFromLegacy` without aligned progress/step
- `DeferredToTerminal` before legacy-stage progress has been reached

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_routing_snapshot_alignment_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_routing_snapshot_alignment_green.exe"
```

## Next

The next useful step is to add the remaining symbi endpoint alignment and then decide whether the outer routing helper is now constrained enough to start moving zygote-control-specific route boundary semantics out of helper-local flow.
