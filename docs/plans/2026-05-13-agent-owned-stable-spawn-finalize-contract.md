# Agent-Owned Stable Spawn Finalize Contract

## Context

The first `agent-owned stable spawn` slice made spawn ownership explicit inside
`SpawnExecutionState`.

That solved the route-commit side of ownership, but `FinalizeSpawn()` was still
branching directly on backend-specific state:

- `owned_spawn_state.backend == kZygoteControl`
- `owned_spawn_state.backend == kSymbi`
- `owned_spawn_state.backend == kLegacyNcore`

Operationally that still worked, but the finalize contract remained
backend-first instead of owner-first.

## Change

Added `ResolveOwnershipStateFromBackend()` and switched `FinalizeSpawn()` to
derive an explicit finalize owner before teardown dispatch.

Current finalize flow now uses:

- `SpawnOwnershipState::kZygoteControlOwned`
- `SpawnOwnershipState::kSymbiOwned`
- `SpawnOwnershipState::kLegacyOwned`

to select the teardown path, instead of branching directly on backend enum
comparisons in the main control flow.

The actual teardown implementations are unchanged:

- zygote-control -> direct zygote-control teardown
- symbi -> cleanup materialized agent artifact only
- legacy -> direct clear path

## Why It Matters

This is a contract refactor, not a behavior change.

It moves one more piece of the spawn lifecycle away from backend-specific
inspection and toward explicit ownership semantics. That matters because later
`agent-owned stable spawn` work should operate in terms of:

- who owns the spawn
- who owns teardown
- which side is authoritative for lifecycle completion

instead of repeatedly inferring lifecycle behavior from backend enums embedded in
different structures.

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
  -o build/test_ninjector_spawn_injector_owner_finalize_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_owner_finalize_green.exe"
```
