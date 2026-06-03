# Zygote-Control Route State Transition Table

## Context

`ApplySpawnRoutingSnapshot()` had already validated several zygote-control route
states against outer routing context:

- `kEntered`
- `kCommitted`
- `kSkipped`
- `kDeferredToFallback`
- `kAborted`

But it still did not fully constrain the route-state machine itself. That meant a
snapshot could carry a structurally impossible zygote-control state jump as long
as the surrounding routing context happened to look valid.

Example of the gap:

- `kSkipped -> kEntered`
- `kEntered -> kSkipped`

Those transitions cannot happen in the real `ApplySpawnRoutingAttempts()` flow,
but the helper still accepted them.

## Change

Added host-side tests to reject impossible `zygote_control_route_state`
transitions:

- reject `kSkipped -> kEntered`
- reject `kEntered -> kSkipped`

Then tightened `ApplySpawnRoutingSnapshot()` with an explicit route-state
transition table:

- `kNotStarted -> kNotStarted | kSkipped | kEntered`
- `kSkipped -> kSkipped`
- `kEntered -> kEntered | kCommitted | kDeferredToFallback | kAborted`
- `kCommitted -> kCommitted`
- `kDeferredToFallback -> kDeferredToFallback`
- `kAborted -> kAborted`

This keeps the helper aligned with the actual zygote-control routing behavior:

- skipped routes stay skipped
- entered routes may only resolve forward
- terminal zygote-control route outcomes do not reopen or reclassify

## Why It Matters

This is another step in making the host-side routing model authoritative instead
of helper-call-order dependent.

Without this constraint, later refactors could accidentally synthesize impossible
route histories even when individual snapshot fields looked locally valid.

With the transition table in place, `zygote_control_route_state` now behaves
closer to a real route-local FSM, which is necessary before collapsing more
zygote-control semantics into explicit host-owned state transitions.

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
  -o build/test_ninjector_spawn_injector_zygote_route_transition_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_zygote_route_transition_green.exe"
```
