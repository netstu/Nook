# Agent-Owned Stable Spawn Deferred Owner Release Helper

## Context

The host-side spawn path had already moved several lifecycle boundaries behind
explicit helpers, but `Spawn()` still performed one owner/session transition
inline when routing deferred to terminal classification:

- match active owner by identifier + spawn token
- clear `active_spawn_owner_`

This was no longer just a local cleanup detail. It was a lifecycle transition
between route handling and terminal handling, and belonged in an explicit helper.

## Change

Extracted explicit helper:

- `ReleaseActiveOwnerAfterDeferredRouting()`

`Spawn()` now delegates deferred-route owner release to this helper instead of
performing the `transaction_mutex_` / `active_spawn_owner_` mutation inline.

The helper owns the contract:

- only release when identifier and spawn token match the active owner
- otherwise preserve active owner state

## Why It Matters

This is the first helper in this sequence that is directly about active
owner/session transition, not just outcome writing.

It pushes the codebase one step closer to a true `agent-owned stable spawn`
shape, where host-side lifecycle transitions are explicit and local reasoning
does not depend on scanning large control-flow functions for hidden owner-state
mutations.

## Verification

Red:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_release_owner_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `ReleaseActiveOwnerAfterDeferredRouting`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_release_owner_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_release_owner_green.exe"
```
