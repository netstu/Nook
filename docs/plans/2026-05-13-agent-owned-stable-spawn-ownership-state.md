# Agent-Owned Stable Spawn Ownership State

## Context

The host-side spawn model had already converged on:

- explicit route phase
- explicit routing state/progress/step
- explicit zygote-control route state
- explicit pending commit payload

But successful spawn ownership was still only implicit:

- `pending_commit.spawn_state.backend`
- `active_spawn_state_.backend`

That was enough for finalize behavior, but not enough for the next
`agent-owned stable spawn` stage where ownership should be visible directly in
the host-side execution model instead of being reconstructed from nested commit
payloads.

## Change

Added explicit ownership state to `SpawnExecutionState`:

- `SpawnOwnershipState::kNone`
- `SpawnOwnershipState::kZygoteControlOwned`
- `SpawnOwnershipState::kSymbiOwned`
- `SpawnOwnershipState::kLegacyOwned`

Also added `TransitionSpawnOwnershipState()` and integrated ownership into
`ApplySpawnRoutingSnapshot()` so ownership is now a validated part of host-side
route commit semantics.

Current alignment rules:

- zygote-control committed route requires zygote-control ownership
- symbi committed route requires symbi ownership
- legacy committed route requires legacy ownership
- ownership may be assigned once, but cannot be rewritten to a different owner

Route commit wiring was updated so successful commits now explicitly bind owner:

- zygote-control success -> `kZygoteControlOwned`
- symbi success -> `kSymbiOwned`
- legacy success -> `kLegacyOwned`

## Why It Matters

This is the first concrete slice of `agent-owned stable spawn`.

Before this change:

- finalize ownership existed operationally
- but only inside nested pending commit/backend structures

After this change:

- ownership is part of the host execution state itself
- route commit semantics and teardown ownership are now on the same host-side
  state surface

That reduces the amount of backend inference needed in later refactors and
creates the state anchor needed for moving toward true agent-owned spawn control.

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
  -o build/test_ninjector_spawn_injector_spawn_ownership_green2.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_spawn_ownership_green2.exe"
```
