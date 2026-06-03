# Agent-Owned Stable Spawn Admission Gate

## Context

After extracting top-level post-routing orchestration from `Spawn()`, the entry
function still had one inline owner/session concern at the top:

- reject a new spawn when an active owner is already present

This was effectively an admission gate for the spawn lifecycle and belonged in
an explicit helper, not as a remaining inline mutex check in the top-level
entrypoint.

## Change

Extracted explicit helper:

- `AdmitSpawnRequest()`

`Spawn()` now delegates active-owner admission to this helper instead of
performing the `transaction_mutex_` / `active_spawn_owner_` check inline.

The helper owns the admission contract:

- reject same-identifier active owner with
  `spawn already active for identifier`
- reject foreign active owner with
  `spawn already active`

## Why It Matters

This finishes another piece of `Spawn()` top-level owner/session cleanup.

At this point `Spawn()` is materially cleaner:

- input validation
- execution state construction
- admission gate
- route application
- post-routing orchestration

That is a much more defensible top-level structure for the
`agent-owned stable spawn` line than the original inline control flow.

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
  -o build/test_ninjector_spawn_injector_admit_spawn_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `AdmitSpawnRequest`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_admit_spawn_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_admit_spawn_green.exe"
```
