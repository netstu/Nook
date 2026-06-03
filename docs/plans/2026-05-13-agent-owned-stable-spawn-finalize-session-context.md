# Agent-Owned Stable Spawn Finalize Session Context

## Context

After top-level finalize orchestration had been split into owned vs unowned
paths, `FinalizeSpawn()` still opened by materializing multiple parallel locals
from `TakeActiveOwnerForFinalize()`:

- finalize owner
- owned spawn state
- owned zygote transaction
- foreign-owner flag
- residual-zgote-target flag

That meant the control-flow had improved, but the finalize entrypoint still did
not carry an explicit session object for the lifecycle state it was branching
on.

## Change

Introduced:

- `FinalizeSession`
- `BuildFinalizeSession()`

`FinalizeSpawn()` now builds a single finalize session context and branches off
that object instead of juggling multiple parallel locals.

The finalize session captures:

- ownership decision
- owned spawn/session payload
- foreign-owner state
- residual zygote-control target state

## Why It Matters

This is a structural step, not just another helper extraction.

The finalize entrypoint now reads more like an actual owner/session lifecycle:

1. build finalize session
2. if owned -> finalize owned backend
3. else -> run unowned finalize orchestration

That is a much cleaner representation of the intended `agent-owned stable
spawn` model than the earlier tuple-of-locals style.

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
  -o build/test_ninjector_spawn_injector_finalize_session_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `BuildFinalizeSession`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_finalize_session_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_finalize_session_green.exe"
```
