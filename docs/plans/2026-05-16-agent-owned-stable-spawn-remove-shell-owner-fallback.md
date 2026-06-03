# 2026-05-16 Agent-Owned Stable Spawn: Remove Shell Owner Fallback

## Context

After introducing explicit `shell_owner_state`, the main owner-sensitive paths had already been
rewired to prefer it:

- admission
- finalize extraction
- deferred owner release

But one compatibility bridge still remained:

- `ResolveAuthoritativeShellSpawnOwner(...)` could still fall back to old `spawn_state`

That meant the code had an explicit owner slot in storage, but the authoritative read path could
still accept old mixed-state inputs.

## Change

Removed the fallback from `ResolveAuthoritativeShellSpawnOwner(...)`:

- authoritative shell ownership now resolves from `shell_owner_state` only
- old `spawn_state` is no longer consulted by owner-sensitive paths

To keep host regression coverage valid, remaining tests that manually construct authoritative shell
owner state were migrated to seed:

- `active_spawn_owner_.shell_owner_state`

instead of relying on `spawn_state` alone.

## Result

This completes the first real cutoff in the migration:

- `shell_owner_state`
  - authoritative shell owner only
- `spawn_state`
  - compatibility/session-local shell data only
- `zygote_control_transaction`
  - zygote-owned spawn record

At this point the main ownership decisions no longer depend on the old mixed shell field.

## Verification

Added/kept regression coverage showing:

- explicit `shell_owner_state` wins over conflicting legacy `spawn_state`
- spawn admission still rejects when explicit shell owner exists
- compatibility-only shell state does not block admission

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Remaining cleanup is mostly structural follow-through:

1. stop mirroring authoritative shell owner into `spawn_state` where unnecessary
2. reduce assertions and helper APIs that still inspect old `spawn_state` for owner-like content
3. continue toward a model where zygote-control commits populate compatibility shell data only when
   actually needed by downstream cleanup or session-local bookkeeping
