# 2026-05-12 Symbi Stub Helper Surface Reduction Status

## Context

This is the first concrete `A1` reduction pass from:

- [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md)

The goal was not to fully redesign the stub in one step. It was to remove helpers and callback payload fields that were clearly no longer required by the hot path.

## What Changed

Files:

- [stub.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi/stub_src/stub.h)
- [stub.c](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi/stub_src/stub.c)
- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)
- [test_symbi_stub_minimal_helpers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_symbi_stub_minimal_helpers.cpp)

Removed from the remote stub/helper surface:

- `getppid`
- callback header `ppid`
- callback header `package_name_len`
- callback package string payload

After this change, the callback contract only carries:

- child `pid`
- `load_ok`

And the stub helper set keeps only what is still needed for the current hot path:

- `getuid`
- `getpid`
- `socket`
- `connect`
- `write`
- `read`
- `close`
- `raise`

## Why These Were Safe To Remove

These fields were not part of the active decision path anymore:

- `ppid` was logged but not used for routing or validation
- callback package payload was echoed back but not used for child delivery or spawn routing

So they increased zygote-side dependency surface and callback protocol size without contributing to the current handoff contract.

## Verification

Minimal helper-surface test:

```powershell
g++ -std=c++17 -I . -I include -I src tests/headers/test_symbi_stub_minimal_helpers.cpp -o tests/headers/test_symbi_stub_minimal_helpers.exe
tests\headers\test_symbi_stub_minimal_helpers.exe
```

API boundary regression:

```powershell
g++ -std=c++17 -I . -I include -I src tests/headers/test_symbi_injector_api_surface.cpp -c -o tests/headers/test_symbi_injector_api_surface.o
```

Host spawn regression:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector.exe
build\test_ninjector_spawn_injector.exe
```

Result:

- all verification steps passed

## Current Position

This pass reduces the local stub dependency surface without changing the gate/handoff split introduced in `A2`.

Remaining likely `A1` candidates still worth reviewing later:

- whether `read` can be removed by making callback fire-and-forget
- whether `getuid` can be replaced by a cheaper child identity check
- whether callback socket helpers can be narrowed further without losing host-ack safety
