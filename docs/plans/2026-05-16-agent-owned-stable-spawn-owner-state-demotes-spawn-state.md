## Goal

Continue converging `agent-owned stable spawn` by demoting `ActiveSpawnOwner::spawn_state`
from authoritative ownership storage into compatibility/session-local runtime state.

## What changed

- `BuildPendingSpawnCommit(...)` now writes authoritative legacy/symbi owner identity into
  `shell_owner_state`.
- For non-zygote-control commits, `spawn_state` is normalized to runtime/session data only:
  it keeps fields like `spawn_token` and cleanup/materialization data, but no longer carries
  authoritative `backend` / `identifier`.
- `CommitPendingSpawn(...)` no longer derives ownership from `spawn_state`; it accepts the
  already-constructed ownership split.
- `TakeActiveOwnerForFinalize(...)` no longer depends on
  `active_spawn_owner_.spawn_state.identifier/backend` to decide ownership. It now resolves
  ownership from:
  1. `zygote_control_transaction`
  2. `shell_owner_state`
  3. compatibility/session data only as payload to clear
- Deferred-release and finalize-related tests were updated so setup expresses authoritative
  ownership through `shell_owner_state`, while `spawn_state` only carries session-local token
  or cleanup data.

## Why

Before this step, the state machine still allowed `spawn_state` to act like a second owner
record. That kept `zygote-control`, legacy shell ownership, and compatibility state coupled.

This separation is required before `zygote-control` can become the real owner of spawn
lifecycle, because otherwise finalize/admission/deferred-release paths can observe mixed state
and make inconsistent ownership decisions.

## Current model

- `shell_owner_state`
  - authoritative owner for legacy/symbi shell-backed spawn
- `zygote_control_transaction`
  - authoritative owner for zygote-control transaction-backed spawn
- `spawn_state`
  - session-local compatibility/runtime payload only

## Verification

Host regression command:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe

build\test_ninjector_spawn_injector_finalize_identity.exe
```

Passed after updating commit/finalize/deferred-routing/admission expectations.

## Next

- Remove remaining redundant `spawn_state` owner-style setup from older tests.
- Continue tightening `FinalizeSession` / deferred-routing semantics around the authoritative
  pair: `zygote_control_transaction` vs `shell_owner_state`.
- Then move into the actual `zygote-control` lifecycle handoff work, with a cleaner owner
  model underneath.
