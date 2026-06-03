# Agent-Owned Stable Spawn Pending Owner Alignment

## Context

After introducing:

- explicit ownership state
- owner-first finalize contract
- unified active owner record

the remaining structural mismatch was `PendingSpawnCommit`.

It still represented the same conceptual data as the active owner record, but as
a separate parallel structure:

- `spawn_state`
- `zygote_control_transaction`
- `has_zygote_control_transaction`

without explicit ownership state on equal footing.

## Change

Aligned pending commit semantics with active owner semantics by collapsing
`PendingSpawnCommit` onto the same owner record shape:

- `PendingSpawnCommit` now aliases `ActiveSpawnOwner`

and successful commit construction now explicitly sets:

- `pending_commit.ownership_state`

before the pending owner is committed into the active owner record.

This also allowed `CommitPendingSpawn()` to stop rebuilding ownership from the
backend and instead transfer the already-formed pending owner record directly.

## Why It Matters

This completes the owner/session shape across the successful spawn lifecycle:

- pending owner
- active owner
- finalize owner

now all share the same semantic structure instead of being represented by three
slightly different models.

That is an important prerequisite for later moving more lifecycle authority into
an owner/session abstraction without additional translation layers.

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
  -o build/test_ninjector_spawn_injector_pending_owner_green2.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_pending_owner_green2.exe"
```
