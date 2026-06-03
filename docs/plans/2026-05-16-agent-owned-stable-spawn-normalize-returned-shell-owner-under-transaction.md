# 2026-05-16 Agent-Owned Stable Spawn: Normalize Returned Shell Owner Under Transaction

## Context

Two earlier steps had already reduced zygote-control shell authority:

- matching zygote transaction overrides shell owner when deciding `finalize_owner`
- committed active owner state is normalized so committed zygote ownership lives in
  `zygote_control_transaction`

However, one remaining leak still existed in the finalize extraction path:

- `TakeActiveOwnerForFinalize(...)` could extract a same-request `spawn_state`
- then also detect that a matching `zygote_control_transaction` actually owns the request
- `finalize_owner` would be corrected to `kZygoteControlOwned`
- but the returned `owned_spawn_state.backend` could still carry an old shell backend such as
  `kLegacyNcore`

That meant upper layers received a normalized owner decision but a non-normalized compatibility
shell record.

## Change

Updated `TakeActiveOwnerForFinalize(...)` so that when a matching zygote transaction owns the
request:

- `finalize_owner` is forced to `kZygoteControlOwned`
- returned `owned_spawn_state.backend` is normalized to `kNone`

Other compatibility shell fields remain available:

- `identifier`
- `spawn_token`
- artifact/session-local data if present

This keeps useful compatibility data while removing the stale backend owner signal.

## Result

Finalize extraction now has a cleaner contract:

- zygote transaction decides zygote-control ownership
- returned shell data no longer claims a legacy/symbi backend when transaction already owns the
  request
- downstream finalize logic sees less mixed state and fewer opportunities for owner drift

This is another small but necessary step toward a truly transaction-owned spawn model.

## Verification

Added regression coverage for:

- same-request shell owner with `backend = kLegacyNcore`
- same-request matching zygote transaction
- expected extracted result:
  - `finalize_owner = kZygoteControlOwned`
  - `owned_spawn_state.backend = kNone`

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

The remaining structural question is no longer owner priority alone, but storage shape:

- whether `ActiveSpawnOwner` should continue storing both shell state and transaction state in one
  aggregate
- or whether the next step should explicitly split:
  - shell compatibility record
  - transaction-owned zygote-control record
