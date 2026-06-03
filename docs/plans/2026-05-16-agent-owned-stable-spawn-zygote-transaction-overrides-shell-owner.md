# 2026-05-16 Agent-Owned Stable Spawn: Zygote Transaction Overrides Shell Owner

## Context

`TakeActiveOwnerForFinalize(...)` had already been split into two extraction paths:

- `spawn_state`
- `zygote_control_transaction`

However, there was still one remaining authority leak:

- when both matched the same request
- `spawn_state` was extracted first
- `finalize_owner` was resolved from `spawn_state.backend`
- the later matching zygote transaction only upgraded owner if `finalize_owner == kNone`

This meant a stale or compatibility-only shell backend could still win the finalize owner decision
even when the request had a matching authoritative zygote-control transaction.

That is the wrong direction for agent-owned stable spawn, where zygote-control transaction state
should be more authoritative than the compatibility shell.

## Change

Updated `TakeActiveOwnerForFinalize(...)` so that:

- if `zygote_control_transaction` matches the request
- `finalize_owner` is always forced to `kZygoteControlOwned`

This override happens even if a same-request `spawn_state` was already extracted first.

The spawn shell is still extracted and returned for compatibility data such as:

- `spawn_token`
- materialized artifact paths
- backend-local cleanup fields for non-zygote paths

But it no longer wins the owner decision when a matching zygote transaction exists.

## Result

Finalize owner resolution is now closer to the intended hierarchy:

- matching zygote-control transaction is authoritative
- spawn shell is compatibility/session-local data only
- mixed same-request shell+transaction state no longer resolves to legacy/symbi owner by mistake

This is another necessary reduction of shell authority before moving into a real
agent-owned stable spawn model.

## Verification

Added regression coverage for the mixed-owner case:

- shell owner backend = `kLegacyNcore`
- matching zygote transaction present for the same identifier
- expected finalize owner = `kZygoteControlOwned`

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Continue shrinking `spawn_state.backend` as an ownership signal for `zygote-control`:

- keep it meaningful for legacy/symbi only
- treat zygote-control ownership as transaction-owned by default
- then re-check whether `ActiveSpawnOwner` should be split into explicit shell-owner and
  transaction-owner records
