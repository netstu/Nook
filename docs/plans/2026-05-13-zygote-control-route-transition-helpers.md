# Zygote-Control Route Transition Helpers

## Context

The host-side spawn routing model already had:

- explicit execution phase
- explicit outer routing state
- explicit routing progress
- explicit current route step
- explicit zygote-control route state

But `ApplySpawnRoutingAttempts()` still directly assembled zygote-control route
snapshots inline at each write site. That meant the host-side route semantics
were explicit in data shape, but still partially implicit in call-site-local
snapshot construction.

## Change

Introduced dedicated host-side zygote-control route transition helpers:

- `BeginSpawnRouting()`
- `EnterZygoteControlRoute()`
- `SkipZygoteControlRoute()`
- `AbortZygoteControlRoute()`
- `CommitZygoteControlRoute()`
- `DeferZygoteControlRouteToFallback()`
- `AdvancePastZygoteControlRoute()`

Then switched the zygote-control portion of `ApplySpawnRoutingAttempts()` to use
these helpers instead of writing raw `SpawnRoutingSnapshot` structures inline.

## Why It Matters

This is the first real step beyond validation-only hardening.

Before this change, host-side routing correctness depended on:

- `ApplySpawnRoutingSnapshot()` validation
- plus the correctness of many handwritten call-site snapshots

After this change, the zygote-control route now has an explicit host-owned
transition surface. That reduces duplication and makes the intended route FSM
visible in one place instead of being reassembled at every write site.

This is the required direction before moving into `agent-owned stable spawn`,
because that later work needs a stable host-side control plane:

- route entry
- route commit
- route abort
- route defer
- route advance

must already be modeled as host-owned transitions rather than scattered field
writes.

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
  -o build/test_ninjector_spawn_injector_zygote_route_helpers_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_zygote_route_helpers_green.exe"
```
