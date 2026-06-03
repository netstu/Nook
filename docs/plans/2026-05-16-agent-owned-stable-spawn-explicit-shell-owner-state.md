# 2026-05-16 Agent-Owned Stable Spawn: Explicit Shell Owner State

## Context

Earlier work had already separated shell state semantically:

- authoritative shell owner required `backend != kNone`
- compatibility shell state no longer blocked admission or acted as foreign owner

But storage still kept both meanings in a single field:

- `ActiveSpawnOwner::spawn_state`

That meant the code had to keep inferring whether `spawn_state` meant:

- real shell owner
- compatibility/session-local shell data

This was workable, but still too implicit for the agent-owned stable spawn direction.

## Change

Introduced an explicit second shell slot in `ActiveSpawnOwner`:

- `shell_owner_state`
- existing `spawn_state` remains as compatibility/session-local shell state

Then migrated the key ownership-sensitive paths to prefer the explicit slot:

- `CommitPendingSpawn(...)`
  - seeds `shell_owner_state` from the pending shell record
  - clears `shell_owner_state` for zygote-owned commits
- `AdmitSpawnRequest(...)`
  - resolves active shell owner through the explicit slot first
- `TakeActiveOwnerForFinalize(...)`
  - extracts authoritative shell owner from the explicit slot
  - recomputes foreign-owner status after mutation using the explicit slot
- `ReleaseActiveOwnerAfterDeferredRouting(...)`
  - matches shell owner through the explicit slot

To keep the transition incremental, an internal resolver now provides compatibility fallback:

- prefer `shell_owner_state`
- fall back to old `spawn_state` only if it still carries authoritative shell-owner data

This avoids breaking hand-constructed older tests and intermediate call paths while the migration is
still in progress.

## Result

`ActiveSpawnOwner` now has an explicit structural split:

- `shell_owner_state`
  - authoritative shell owner record
- `spawn_state`
  - compatibility/session-local shell data
- `zygote_control_transaction`
  - zygote-owned transaction record

This is the first real storage-level move toward agent-owned stable spawn, not just policy cleanup.

## Verification

Added regression coverage requiring `CommitPendingSpawn(...)` to separate:

- authoritative shell owner state
- compatibility shell state

Then updated relevant host tests to populate `shell_owner_state` where they manually construct
authoritative shell ownership.

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

The migration is not finished yet. Remaining work is to reduce fallback dependence on old
`spawn_state` by:

1. moving more manual owner construction sites to `shell_owner_state`
2. shrinking `ResolveAuthoritativeShellSpawnOwner(...)` fallback usage
3. eventually stopping owner-sensitive paths from consulting compatibility `spawn_state` at all
