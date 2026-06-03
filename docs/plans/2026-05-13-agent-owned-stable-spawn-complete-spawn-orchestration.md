# Agent-Owned Stable Spawn Complete Spawn Orchestration

## Context

After route-commit transition helpers and terminal helpers were in place,
`Spawn()` still performed its post-routing orchestration inline:

- committed-route completion
- deferred-route owner release
- terminal classification/finalize path
- completed-phase transition

That meant `FinalizeSpawn()` had already been raised into clearer top-level
branches, but `Spawn()` was still carrying the same style of inline lifecycle
choreography.

## Change

Extracted a higher-level helper:

- `CompleteSpawnAfterRouting()`

`Spawn()` now delegates post-routing orchestration to this helper.

The helper owns the two top-level post-routing branches:

1. committed route -> completed
2. deferred route -> release active owner -> terminal outcome -> completed

## Why It Matters

This is the natural counterpart to `FinalizeWithoutOwnedBackend()`.

At this point both top-level entrypoints now have clearer orchestration
boundaries:

- `Spawn()` delegates post-routing lifecycle orchestration
- `FinalizeSpawn()` delegates unowned finalize orchestration

This is meaningfully closer to the intended `agent-owned stable spawn` shape,
where top-level control flow describes owner/session lifecycle phases instead of
embedding detailed state transitions inline.

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
  -o build/test_ninjector_spawn_injector_complete_spawn_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `CompleteSpawnAfterRouting`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_complete_spawn_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_complete_spawn_green.exe"
```
