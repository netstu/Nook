# 2026-05-12 Spawn Backend Responsibility Clarification Status

## Context

This is the `B1` follow-up from:

- [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md)

The goal here was not to change spawn behavior. It was to make backend responsibilities and fallback intent explicit in code.

## What Changed

Files:

- [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Added code-surface clarification for:

- `SpawnBackend::kSymbi`
  - zygote gate path
  - owns spawn-time handoff, not prepare/clear lifecycle
- `SpawnBackend::kLegacyNcore`
  - stable prepare/clear path
  - explicitly the durable fallback backend
- `SpawnBackend::kZygoteControl`
  - experimental agent-controlled path
  - intentionally separate from legacy prepare/clear semantics

Also clarified at the `Spawn()` call site:

- when symbi is explicitly requested
- when default routing prefers symbi first
- when experimental zygote-control is attempted
- where legacy fallback responsibility begins

And clarified at `FinalizeSpawn()`:

- zygote-control owns its own teardown
- symbi finalization is just artifact cleanup
- legacy ncore finalization owns prepare/clear teardown

## What Did Not Change

No backend policy changed in this step.

In particular:

- default routing still prefers symbi when `enable_zygote_control == false`
- legacy ncore remains the current stable fallback
- zygote-control remains experimental

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector.exe
build\test_ninjector_spawn_injector.exe
```

Result:

- host test executable completed successfully
