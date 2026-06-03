# Spawn current route step model

## Context

The outer host-side spawn model already tracked:

- `routing_state`
- `routing_progress`
- `routing_windows`

That was enough to answer:

- what final routing state was reached
- how far the ordered routing walk progressed
- whether each route window was entered, skipped, or only probed

One piece was still missing:

which route step is currently being evaluated, or equivalently, what the last active route step was when the helper exited.

## Change

Added `SpawnRouteStep` and `current_route_step` to `SpawnExecutionState`.

Current values:

- `kNone`
- `kZygoteControl`
- `kSymbi`
- `kLegacy`

## Current meaning

`current_route_step` is intentionally not a full history.

It means:

- before routing starts: `kNone`
- during routing: the route step currently being evaluated
- after routing exits: the last route step that was actually processed

Examples:

- zygote-control success: `current_route_step = kZygoteControl`
- deferred route after legacy fallback attempt: `current_route_step = kLegacy`
- legacy probe-only terminal classification support: `current_route_step = kLegacy`

## Why this matters

This fills the last obvious readability gap in the outer routing surface.

Now the host-side state can answer all of these separately:

- `routing_state`: what high-level routing result was reached
- `routing_progress`: how far the ordered routing walk advanced
- `routing_windows`: what happened to each route window
- `current_route_step`: which route step was active most recently

That is enough structure to start pulling zygote-control routing boundaries out of helper-local flow and into explicit state semantics.

## Tests

Updated white-box coverage to assert:

- initial `current_route_step = kNone`
- zygote-control success leaves `current_route_step = kZygoteControl`
- deferred routing after legacy processing leaves `current_route_step = kLegacy`

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_route_step_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_route_step_green.exe"
```

## Next

The next useful step is to introduce a small explicit routing snapshot/update helper so the outer route state stops being written piecemeal in `ApplySpawnRoutingAttempts(...)` and instead advances through a single host-side surface.
