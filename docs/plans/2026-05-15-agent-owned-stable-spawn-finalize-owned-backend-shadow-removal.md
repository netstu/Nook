# 2026-05-15 Agent-Owned Stable Spawn: Finalize Owned-Backend Shadow Removal

## Context

After removing duplicated zygote transaction presence flags, `FinalizeSpawn()` still carried one more derived boolean:

- `has_owned_backend`

This value was computed from:

- `session.finalize_owner != SpawnOwnershipState::kNone`

and then forwarded into `FinalizeWithoutOwnedBackend(...)`, even though that function is only called when `finalize_owner == kNone`.

That made the finalize probe path look more stateful than it really is.

## Change

Removed `has_owned_backend` from the finalize path:

- `FinalizeWithoutOwnedBackend(...)` no longer accepts it
- `FinalizeSpawn()` no longer computes or forwards it
- `ShouldProbeZygoteControlFinalizeFallback(...)` now depends only on:
  - `enable_zygote_control`
  - `has_residual_zygote_control_targets`

## Result

Finalize fallback probing is now driven only by actual residual state, instead of a derived boolean that was constant on that call path.

This further narrows the ownership/finalize state surface while keeping runtime behavior unchanged.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Next candidate to inspect:

- `FinalizeSession::has_foreign_active_owner`

That one may or may not still deserve to exist depending on whether the remaining active-owner state can be expressed directly from the retained owner record without widening the finalize API again.
