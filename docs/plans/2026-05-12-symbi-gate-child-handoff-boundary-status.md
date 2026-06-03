# 2026-05-12 Symbi Gate Child Handoff Boundary Status

## Context

This is the `A2` follow-up from:

- [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md)

The goal here was to make the gate-vs-handoff boundary explicit in code, not to redesign the full symbi path.

## What Changed

Files:

- [symbi_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi/symbi_injector.h)
- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)
- [ninjector_compat.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_compat.cpp)
- [test_symbi_injector_api_surface.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_symbi_injector_api_surface.cpp)

Core change:

- removed `so_path` from the local symbi gate APIs:
  - `inject_spawn_symbi_by_package()`
  - `inject_spawn_symbi_by_pids()`

This makes the boundary explicit:

- local symbi injector owns:
  - zygote-side gate preparation
  - callback listener setup
  - child callback wait
  - zygote restore
  - returning child identity
- caller-owned compat layer owns:
  - host-side child runtime delivery
  - sidecar `dlopen` injection for `SpawnViaSymbi`
  - child-owned memfd delivery for `SpawnViaSymbiEmbedded`
  - final child resume

Also added code comments in both layers to mark that handoff boundary directly at the relevant call sites.

## Why This Matters

Before this change, the local symbi API still accepted `so_path`, which implied that child runtime delivery belonged to the same conceptual stage as zygote gate installation.

That was misleading:

- the local symbi path never used `so_path`
- the real child runtime delivery always happened later in `ninjector_compat.cpp`

Removing the dead parameter makes the separation enforceable at compile time.

## Verification

Header/API surface:

```powershell
g++ -std=c++17 -I . -I include -I src tests/headers/test_symbi_injector_api_surface.cpp -c -o tests/headers/test_symbi_injector_api_surface.o
```

Spawn host test:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector.exe
build\test_ninjector_spawn_injector.exe
```

Result:

- API surface test compiles successfully
- host spawn test executable completed successfully

## Current Position

The code now encodes the intended boundary:

- `symbi_injector_local.cpp` = zygote gate + callback/restore handoff
- `ninjector_compat.cpp` = child runtime delivery after child stop

This is the minimum structural cleanup needed before going deeper into `A1` and `A3`.
