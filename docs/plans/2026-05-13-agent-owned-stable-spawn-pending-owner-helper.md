# Agent-Owned Stable Spawn Pending Owner Helper

## Context

`PendingSpawnCommit` had already been aligned structurally with the unified
active owner record, but successful spawn paths were still manually assembling
that owner state inside `CommitSuccessfulSpawnOutcome()`.

That left one owner/session boundary implicit:

- successful backend result
- manual pending owner field assignment
- commit into active owner storage

The shape was correct, but the contract was still local and ad hoc.

## Change

Extracted explicit owner helper:

- `BuildPendingSpawnCommit()`

`CommitSuccessfulSpawnOutcome()` now delegates pending owner formation to that
helper before committing it into `active_spawn_owner_`.

The helper owns:

- mapping backend -> ownership state
- binding identifier/backend into `spawn_state`
- attaching zygote-control owned transaction only for zygote-control ownership

That means non-zygote owners no longer implicitly carry irrelevant zygote
transaction session state through success-path assembly.

## Why It Matters

This is the next cleanup step in `agent-owned stable spawn`.

The host-side lifecycle model is now more explicit at each boundary:

- route success
- pending owner construction
- active owner commit
- finalize owner extraction
- owner-owned teardown

That reduces backend-specific manual success-path assembly and gives the next
refactor step a cleaner surface for moving more success handling toward
owner/session helpers.

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
  -o build/test_ninjector_spawn_injector_pending_owner_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `BuildPendingSpawnCommit`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_pending_owner_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_pending_owner_green.exe"
```
