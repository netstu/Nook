# Spawn routing snapshot progress-step validation

## Context

`ApplySpawnRoutingSnapshot(...)` had already moved from:

- raw write helper

to:

- minimally validated write helper

with one legality rule on `routing_state`.

That was not enough once the outer model also started to rely on:

- `routing_progress`
- `current_route_step`

Without basic checks, these fields could still drift into impossible combinations even if `routing_state` itself looked valid.

## Change

Extended `ApplySpawnRoutingSnapshot(...)` with two additional narrow validations:

1. `routing_progress`

- from `kNotStarted`, the helper only allows staying at `kNotStarted` or moving to `kEnteredRouting`
- direct jumps to `kAfterZygoteControl`, `kAfterSymbi`, or `kAfterLegacy` are rejected

2. `current_route_step`

- when the current progress is still `kNotStarted`
- and the snapshot is not also advancing progress in the same call
- a non-`kNone` route step is rejected

Rejected writes still return:

- `false`
- `error_message = "invalid spawn routing snapshot transition"`

## Why this matters

This starts to align the outer routing model along three dimensions at once:

- routing state
- routing progress
- current route step

It is still intentionally incomplete, but now the helper rejects a few more obviously impossible route-state combinations before they can spread through later refactors.

## Tests

Added white-box coverage for:

- rejecting `routing_progress: kNotStarted -> kAfterSymbi`
- rejecting `current_route_step = kLegacy` while progress has not started

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_routing_snapshot_validation_green2.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_routing_snapshot_validation_green2.exe"
```

## Next

The next useful step is to validate cross-field alignment for committed/deferred routing endpoints, so `routing_state`, `routing_progress`, and `current_route_step` cannot disagree about where routing actually stopped.
