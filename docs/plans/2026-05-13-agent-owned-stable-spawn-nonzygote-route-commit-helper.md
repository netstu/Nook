# Agent-Owned Stable Spawn Non-Zygote Route Commit Helper

## Context

Zygote-control already had an explicit route-commit transition helper:

- `CommitZygoteControlRoute()`

But symbi and legacy successful route commits were still performing their own
phase/ownership/routing-state transitions inline inside
`ApplySpawnRoutingAttempts()`.

That left the host-side route lifecycle split across:

- one explicit zygote-control commit helper
- two inline non-zygote committed-route transition sequences

## Change

Extracted a unified committed-route transition helper for non-zygote backends:

- `CommitNonZygoteControlRoute()`

This helper now owns the committed-route transition contract for:

- `SpawnBackend::kSymbi`
- `SpawnBackend::kLegacyNcore`

It applies:

- route-committed phase transition
- ownership transition
- committed routing-state transition

`ApplySpawnRoutingAttempts()` now delegates non-zygote committed-route
transition to this helper instead of repeating the sequence inline.

## Why It Matters

This is the first refactor in this line that clearly raises the abstraction from
"helper for a single field group" to "helper for a full route transition".

The committed-route lifecycle now has a clearer structure:

- `CommitZygoteControlRoute()` for the zygote-controlled route
- `CommitNonZygoteControlRoute()` for the remaining stable backends

That is much closer to an owner/session transition model and is the clearest
signal so far that this work has moved beyond preparatory cleanup and into the
actual `agent-owned stable spawn` refactor path.

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
  -o build/test_ninjector_spawn_injector_nonzygote_commit_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `CommitNonZygoteControlRoute`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_nonzygote_commit_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_nonzygote_commit_green.exe"
```
