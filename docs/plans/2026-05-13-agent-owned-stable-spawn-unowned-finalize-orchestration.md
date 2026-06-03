# Agent-Owned Stable Spawn Unowned Finalize Orchestration

## Context

After extracting owner/session transition helpers, `FinalizeSpawn()` still
mixed two different levels of logic:

- owned finalize dispatch
- no-owner fallback orchestration across residual zygote-control state and
  legacy clear path

That kept a large block of unowned finalize behavior embedded directly in the
top-level finalize function, even though the underlying lifecycle pieces had
already become explicit.

## Change

Extracted a higher-level orchestration helper:

- `FinalizeWithoutOwnedBackend()`

`FinalizeSpawn()` now reads much closer to a two-path lifecycle:

1. owned finalize path
2. unowned finalize orchestration path

The new helper owns the no-owner flow:

- optional residual zygote-control finalize probe
- foreign-owner early-success handling
- legacy finalize fallback
- final error/log formatting

## Why It Matters

This is a step above earlier helper work.

Previous changes mostly extracted:

- outcome application
- transaction movement
- individual route/finalize transitions

This change extracts an actual top-level lifecycle branch. That is the clearest
signal so far that the code is moving from "helper cleanup" into explicit
owner/session orchestration.

`FinalizeSpawn()` is now materially closer to the intended
`agent-owned stable spawn` shape, where the top-level control flow expresses
owner-driven lifecycle branches instead of embedding a large amount of inline
state choreography.

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
  -o build/test_ninjector_spawn_injector_unowned_finalize_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `FinalizeWithoutOwnedBackend`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_unowned_finalize_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_unowned_finalize_green.exe"
```
