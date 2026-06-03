# 2026-05-15 Agent-Owned Stable Spawn: Owner Admission Transaction Awareness

## Context

While moving toward agent-owned stable spawn, `AdmitSpawnRequest(...)` still treated
`active_spawn_owner_.spawn_state.identifier` as the only authoritative signal that a spawn was in
flight.

That left a gap:

- `active_spawn_owner_` could still contain a residual
  `zygote_control_transaction`
- but if `spawn_state.identifier` was already empty, a new spawn request could be admitted
- which risks overwriting or racing with a still-owned zygote-control transaction

This is a real owner-boundary bug, not just cosmetic duplicate state.

## Change

Updated `AdmitSpawnRequest(...)` so admission now considers both:

- `active_spawn_owner_.spawn_state`
- `active_spawn_owner_.zygote_control_transaction`

Resolution order is:

1. active spawn-state identifier when present
2. otherwise active zygote-control transaction identifier when present

If either indicates an active owner, the request is rejected using the same stable error messages:

- `spawn already active for identifier`
- `spawn already active`

## Result

Spawn admission is now transaction-aware:

- residual zygote-control ownership can block conflicting new spawn requests
- owner admission is better aligned with the actual state carried toward finalize
- this reduces one more place where spawn-state and transaction-state could diverge

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Continue shrinking split owner authority by tightening finalize / release behavior around the
transaction record, not just the spawn-state shell.
