# 2026-05-15 Agent-Owned Stable Spawn: Finalize Owner Partial Extract

## Context

After making admission and deferred release transaction-aware, finalize owner extraction still had
an all-or-nothing behavior:

- `TakeActiveOwnerForFinalize(...)` matched only `spawn_state.identifier`
- when it matched, it cleared the entire `active_spawn_owner_`

That meant a current request could extract its own spawn-state owner and accidentally destroy a
foreign residual zygote transaction carried in the same `active_spawn_owner_`.

This is exactly the kind of mixed ownership boundary that blocks convergence toward
agent-owned stable spawn.

## Change

Refined `TakeActiveOwnerForFinalize(...)` so it now treats spawn-state ownership and
zygote-transaction ownership independently.

Behavior is now:

- if spawn-state matches the request:
  - extract and clear only `spawn_state`
  - resolve `finalize_owner` from the extracted backend
- if zygote transaction also matches the request:
  - extract and clear that transaction too
- if a foreign spawn-state owner or foreign zygote transaction remains:
  - surface `has_foreign_active_owner = true`

## Result

Finalize owner extraction is now aligned with the partial-clear behavior added to deferred release:

- current-request spawn owner can be finalized without erasing foreign residual zygote ownership
- foreign residual transaction state survives until its own finalize/release path handles it
- ownership boundaries are becoming component-wise instead of shell-wise

This is another direct step toward true agent-owned stable spawn semantics.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Re-evaluate whether the remaining `ActiveSpawnOwner` shell is still carrying duplicated authority,
or whether the next transition should move into a more explicit transaction-owned finalize path.
