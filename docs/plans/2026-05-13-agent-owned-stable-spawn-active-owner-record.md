# Agent-Owned Stable Spawn Active Owner Record

## Context

Before this slice, active successful spawn ownership was still split across two
independent fields:

- `active_spawn_state_`
- `active_zygote_control_transaction_`

That meant the host had explicit ownership semantics at the execution-state
layer, but the long-lived active owner view was still physically fragmented.

This was the next structural blocker for `agent-owned stable spawn`, because a
single successful spawn owner should be represented as one host-owned record,
not reconstructed by combining separate active fields.

## Change

Introduced a unified active owner record:

- `ActiveSpawnOwner`
  - `ownership_state`
  - `spawn_state`
  - `zygote_control_transaction`
  - `has_zygote_control_transaction`

Then moved active ownership writes/reads to this single structure:

- `CommitPendingSpawn()`
- `Spawn()` duplicate-active checks and cleanup
- `FinalizeSpawn()` owner extraction and fallback probe path

This is still behavior-preserving:

- zygote-control owners still carry their transaction
- symbi owners still only carry spawn-state cleanup info
- legacy owners still carry clear-path state

But the active owner representation is now unified instead of split.

## Why It Matters

This is the first real data-structure step toward agent-owned stable spawn.

The host now has:

- explicit route ownership in execution state
- explicit owner-first finalize contract
- explicit single active owner record

That is the minimum shape needed before further moving lifecycle authority from
backend-specific logic toward a true owner/session model.

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
  -o build/test_ninjector_spawn_injector_active_owner_green2.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_active_owner_green2.exe"
```
