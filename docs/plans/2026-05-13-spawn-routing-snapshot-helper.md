# Spawn routing snapshot helper

## Context

The outer host-side routing model now had enough surface to be useful:

- `routing_state`
- `routing_progress`
- `current_route_step`
- `routing_windows`

But these fields were still being written piecemeal all over `ApplySpawnRoutingAttempts(...)`.

That meant the model existed, but the write path was still helper-local control flow instead of a single host-side state update surface.

## Change

Added `SpawnRoutingSnapshot` and `ApplySpawnRoutingSnapshot(...)`.

This helper is intentionally small:

- no behavior changes
- no classification logic
- no transition validation yet

It only applies batched outer-routing state updates to:

- `routing_state`
- `routing_progress`
- `current_route_step`
- `routing_windows`

## Why this matters

This is the first real write-side convergence point for the routing model.

Before this step:

- outer route state was readable
- but writes were still scattered

After this step:

- outer route state is readable
- and most route-state writes now flow through one surface

That is the right staging point before introducing stricter route-state semantics or zygote-control-specific boundary logic.

## Verification

No intended behavior change.

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_routing_snapshot_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_routing_snapshot_green.exe"
```

## Next

The next useful step is to add a small route-state transition helper on top of this snapshot surface, so the outer routing model can move from "single write API" to "single validated write API" before deeper zygote-control convergence.
