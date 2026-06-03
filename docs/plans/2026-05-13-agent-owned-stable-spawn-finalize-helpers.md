# Agent-Owned Stable Spawn Finalize Helpers

## Context

After owner-first finalize and unified active owner state were in place,
`FinalizeSpawn()` still bundled three distinct responsibilities in one function:

- extract active owner state for finalize
- dispatch owned teardown by owner
- perform fallback probe / terminal formatting

That meant the data model had improved, but the finalize contract was still not
cleanly expressed as owner/session operations.

## Change

Extracted two explicit finalize helpers:

- `TakeActiveOwnerForFinalize()`
- `FinalizeOwnedSpawnByOwner()`

`FinalizeSpawn()` now reads as:

1. take active owner
2. if there is an owner, let the owner-owned finalize path decide success/failure
3. otherwise fall through to fallback probe / terminal finalize logic

This preserves the prior contract:

- an owned teardown path is authoritative for its own result
- fallback probing is only for the no-owner / residual-target case

## Why It Matters

This is the next owner/session contract cleanup step.

The host-side code now has clearer lifecycle boundaries:

- pending owner formation
- active owner storage
- finalize owner extraction
- finalize owner dispatch

That makes the next stage easier, where more lifecycle authority can be moved
toward owner/session operations instead of ad hoc function-local branching.

## Verification

Host-side compile and test:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_finalize_helpers_green2.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_finalize_helpers_green2.exe"
```
