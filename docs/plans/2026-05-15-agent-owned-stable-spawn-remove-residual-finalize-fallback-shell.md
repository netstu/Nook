# 2026-05-15 Agent-Owned Stable Spawn: Remove Residual Finalize Fallback Shell

## Context

After residual `zygote_control_transaction` state was promoted into the main finalize owner path,
`FinalizeWithoutOwnedBackend(...)` still retained legacy logic for probing zygote-control finalize
from the no-owner path.

At that point the codebase had two ways to represent the same residual transaction finalize:

- the new owner path through `BuildFinalizeSession(...)` ->
  `FinalizeOwnedSpawnByOwner(...)`
- the old fallback probe inside `FinalizeWithoutOwnedBackend(...)`

Keeping both no longer improved coverage. It only preserved duplicate semantics and test burden.

## Change

Removed the residual zygote-control finalize probe from `FinalizeWithoutOwnedBackend(...)`.

That function now only covers the paths that still actually belong there:

- foreign active owner -> no-op success
- legacy finalize attempt -> success or direct legacy error propagation

Residual zygote-control finalize is no longer represented in that branch because it is already
handled through the owned finalize route.

Associated old tests that depended on the fallback-shell semantics were removed.

## Result

Finalize routing is now cleaner:

- residual zygote-control transaction finalize has one real入口
- no-owner finalize is back to meaning “legacy/no-op only”
- the finalize state machine is closer to a single transaction-owned model

This is a direct simplification toward agent-owned stable spawn.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Continue deleting or folding interfaces that only existed to support the removed residual fallback
shell, so the remaining owner model is both structurally and behaviorally singular.
