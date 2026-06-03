# 2026-05-16 Agent-Owned Stable Spawn: Authoritative Shell Owner Gate

## Context

`ActiveSpawnOwner` still stores two kinds of state in one aggregate:

- shell compatibility state in `spawn_state`
- zygote-owned transaction state in `zygote_control_transaction`

Recent work had already reduced zygote-control authority in the shell by:

- normalizing committed zygote shell owner state
- forcing matching zygote transaction to override shell owner during finalize
- normalizing returned shell backend when transaction owns the request

But one structural leak remained:

- several admission/finalize/release checks still treated `spawn_state.identifier != ""` as
  sufficient evidence of an active shell owner

That meant compatibility shell state with:

- `backend = kNone`
- non-empty `identifier`

could still be mistaken for a real owner.

## Change

Introduced an authoritative shell-owner gate:

- shell state only counts as an owner when:
  - `backend != kNone`
  - `identifier` is non-empty

This gate is now used in the key mixed-state decisions:

- `AdmitSpawnRequest(...)`
- `TakeActiveOwnerForFinalize(...)`
- foreign owner detection in finalize extraction
- `ReleaseActiveOwnerAfterDeferredRouting(...)`

## Result

This creates a much cleaner semantic split inside the existing `ActiveSpawnOwner` container:

- authoritative shell owner:
  - legacy/symbi-style owner record
  - requires real backend ownership
- compatibility shell state:
  - may keep token/identifier/session-local fields
  - does not block new spawn admission
  - does not count as foreign owner
  - does not participate in shell-owner matching

This is the first step where shell-vs-transaction separation is enforced by policy, not just by
best-effort normalization.

## Verification

Added regression coverage:

- `AdmitSpawnRequest(...)` must ignore shell state with:
  - `identifier = com.demo.target`
  - `spawn_token = compat-token`
  - `backend = kNone`

Updated older tests that had manually constructed shell owners without a backend so they now model
real authoritative shell ownership explicitly.

Host verification passed:

```powershell
E:\MinGW\ucrt64\bin\\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_identity.exe
build\\test_ninjector_spawn_injector_finalize_identity.exe
```

## Next

The remaining question is whether this policy split is enough, or whether the storage model should
now be made explicit by replacing the current mixed `ActiveSpawnOwner` aggregate with:

- an authoritative shell-owner record
- a compatibility shell/session record
- a transaction-owned zygote-control record
