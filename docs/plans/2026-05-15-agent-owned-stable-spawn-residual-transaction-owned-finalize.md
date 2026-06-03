# 2026-05-15 Agent-Owned Stable Spawn: Residual Transaction Owned Finalize

## Context

Even after admission/release/finalize extraction became transaction-aware, a residual
`zygote_control_transaction` still re-entered finalize through a weaker path:

- `BuildFinalizeSession(...)` left `finalize_owner = kNone`
- residual transaction was passed into `FinalizeWithoutOwnedBackend(...)`
- that path treated it like a fallback probe input instead of true owned zygote-control state

This kept residual transaction handling one step behind the ownership model.

## Change

Updated `BuildFinalizeSession(...)` so a successfully extracted residual
`zygote_control_transaction` is now promoted to:

- `finalize_owner = kZygoteControlOwned`

That means `FinalizeSpawn(...)` will route the request through:

- `FinalizeOwnedSpawnByOwner(...)`
- `FinalizeZygoteControlSpawn(...)`

instead of sending residual zygote transaction state through the fallback-only finalize path.

## Result

Residual zygote transaction state is now treated as real owned finalize state:

- not just probe metadata for fallback
- not a secondary path behind legacy finalize
- aligned with the direction of agent-owned stable spawn

This is the first step where a transaction object is not merely informing cleanup, but directly
deciding the finalize route as an owner.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Now that residual transaction state can own finalize routing, the next transition is to re-check
how much authority still lives in the `ActiveSpawnOwner` shell versus the transaction itself, and
whether the zygote-control owner path can be simplified further around that transaction.
