# 2026-05-15 Agent-Owned Stable Spawn: Finalize Transaction-Authoritative State

## Context

The previous step moved `zygote-control` spawn-attempt lifecycle and failure tracking toward
`ZygoteControlOwnedTransaction`, but finalize still treated injector-global recorder state as the
main mutable surface.

In `FinalizeZygoteControlSpawn(...)` the flow still:

- cleared recorder state up front
- wrote finalize lifecycle/failure into recorder state
- mirrored those values back into `owned_transaction`
- cleared both again on success

That kept finalize transaction state secondary even though finalize already operates on an owned
transaction object.

## Change

Refactored `FinalizeZygoteControlSpawn(...)` so finalize state is expressed first through the
owned transaction and only mirrored into the injector recorder for compatibility.

Added local helpers:

- `set_finalize_lifecycle_state(...)`
- `set_finalize_failure_state(...)`
- `clear_finalize_state()`

These helpers now:

- update `owned_transaction->lifecycle_state`
- update `owned_transaction->failure_state`
- mirror into recorder state only as compatibility support

The finalize path now:

- initializes state through `clear_finalize_state()`
- records `kFinalizeClear` through the transaction-first helper
- records finalize clear failures through the transaction-first helper
- clears both transaction and recorder state through one shared helper on success

## Result

For finalize:

- transaction state is now the first-class representation of lifecycle/failure progression
- injector-global recorder state is reduced to fallback/compat behavior
- spawn-path and finalize-path ownership semantics are now aligned

This keeps the transition toward agent-owned stable spawn coherent instead of letting finalize
re-introduce recorder-first behavior.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Continue reducing recorder authority in:

- finalize fallback state resolution
- failed zygote-control outcome classification
- remaining recorder-seeded tests that should become transaction-seeded once semantics are fully
  converged
