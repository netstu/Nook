# 2026-05-16 Agent-Owned Stable Spawn: Commit Normalizes Zygote Shell Owner

## Context

The main zygote-control success path had already stopped proactively seeding
`spawn_state.backend = kZygoteControl`.

However, there was still a weaker gap:

- `CommitPendingSpawn(...)` blindly copied `pending_commit` into `active_spawn_owner_`
- helper-level tests and compatibility call sites could still provide a shell owner with
  `spawn_state.backend = kZygoteControl`
- that meant a zygote transaction could be authoritative in theory, but the committed owner shell
  could still carry zygote owner semantics in practice

That keeps too much authority in the shell and makes later finalize/release behavior more fragile.

## Change

Updated `CommitPendingSpawn(...)` to normalize committed owner state when a zygote transaction is
present:

- preserve `spawn_state.spawn_token`
- clear `spawn_state.identifier`
- force `spawn_state.backend = kNone`

The committed zygote transaction remains intact and becomes the only committed owner authority for
the zygote-control route.

## Result

Committed active owner state is now closer to the intended split:

- zygote-control ownership lives in `zygote_control_transaction`
- shell state is session-local compatibility data only
- even helper or compatibility paths that still pass a `kZygoteControl` shell into
  `CommitPendingSpawn(...)` get normalized at the write boundary

This reduces another source of ownership drift before the transition into a real
agent-owned stable spawn architecture.

## Verification

Added host regression coverage:

- commit a pending owner record with:
  - `spawn_state.backend = kZygoteControl`
  - matching `zygote_control_transaction`
- verify committed active owner is normalized to:
  - `spawn_state.backend = kNone`
  - empty `spawn_state.identifier`
  - preserved `spawn_state.spawn_token`

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

Continue removing remaining zygote-control ownership dependence on `spawn_state.backend`:

- keep `ResolveOwnershipStateFromBackend(...)` for legacy/symbi shell owners
- avoid using shell backend as a zygote-control owner signal in mixed finalize paths
- then re-evaluate whether `ActiveSpawnOwner` should be split into explicit shell-owner and
  transaction-owner storage
