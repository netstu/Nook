# Spawn routing snapshot symbi endpoint alignment

## Context

The routing snapshot helper already validated endpoint alignment for:

- `CommittedFromZygoteControl`
- `CommittedFromLegacy`
- `DeferredToTerminal`

That still left one missing committed endpoint:

- `CommittedFromSymbi`

Without it, the outer routing model still allowed one backend-specific committed state to disagree with:

- `routing_progress`
- `current_route_step`

while the others were already constrained.

## Change

Added endpoint alignment validation for:

- `SpawnRoutingState::kCommittedFromSymbi`

Required alignment:

- `routing_progress == kAfterSymbi`
- `current_route_step == kSymbi`

If not aligned, `ApplySpawnRoutingSnapshot(...)` rejects the write with:

- `false`
- `error_message = "invalid spawn routing snapshot transition"`

## Why this matters

This closes the obvious gap in committed routing endpoint consistency.

At this point the outer routing snapshot surface now constrains:

- zygote-control committed endpoint
- symbi committed endpoint
- legacy committed endpoint
- deferred-to-terminal endpoint

That means the host-side routing model is no longer only readable. It now enforces a basic consistent interpretation of where routing stopped across all current backend endpoints.

## Tests

Added white-box coverage for rejecting:

- `CommittedFromSymbi` without aligned `routing_progress` and `current_route_step`

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_symbi_alignment_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_symbi_alignment_green.exe"
```

## Next

The next useful step is no longer another tiny endpoint check. The outer routing surface is now constrained enough to begin moving zygote-control route-boundary semantics out of helper-local flow and into explicit host-side state transitions.
