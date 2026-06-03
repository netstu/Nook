# 2026-05-15 Agent-Owned Stable Spawn: Release Owner Partial Clear

## Context

After making spawn admission transaction-aware, deferred-route owner release was still coarse:

- `ReleaseActiveOwnerAfterDeferredRouting(...)` only matched
  `active_spawn_owner_.spawn_state`
- if it matched, it cleared the entire `active_spawn_owner_`

That created two incorrect behaviors:

1. a matching spawn-state release could erase a foreign residual zygote transaction
2. a matching residual zygote transaction could not be released unless the spawn-state shell also
   matched

This kept owner cleanup behind the actual ownership model we are trying to converge on.

## Change

Refined `ReleaseActiveOwnerAfterDeferredRouting(...)` to release ownership by actual matching
surface:

- if both spawn-state and zygote transaction match: clear all owner state
- if only spawn-state matches: clear only `spawn_state`
- if only zygote transaction matches: clear only `zygote_control_transaction`
- otherwise: preserve state and return `false`

## Result

Deferred-route cleanup is now more aligned with transaction-aware ownership:

- foreign residual transactions survive unrelated spawn-state release
- residual zygote transactions can be released independently
- owner cleanup is no longer forced through an all-or-nothing spawn-state gate

This is another concrete step toward `agent-owned stable spawn`, where transaction ownership is not
just metadata but part of the real cleanup boundary.

## Verification

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Continue tightening finalize owner extraction so zygote-control ownership is recognized and
preserved through the transaction boundary, not just the spawn-state shell.
