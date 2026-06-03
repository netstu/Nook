# 2026-05-12 Symbi Fire-And-Stop Callback Status

## Context

This is the next `A1` reduction step after:

- [2026-05-12-symbi-stub-helper-surface-reduction-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-symbi-stub-helper-surface-reduction-status.md)

The target of this pass was the remote `read` dependency in the symbi stub hot path.

## What Changed

Files:

- [stub.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi/stub_src/stub.h)
- [stub.c](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi/stub_src/stub.c)
- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)
- [test_symbi_stub_minimal_helpers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_symbi_stub_minimal_helpers.cpp)

Changed callback model:

- before:
  - child connected to host
  - child sent callback header
  - child blocked on host ack via remote `read`
  - child raised `SIGSTOP` only after ack
- now:
  - child connects to host
  - child sends callback header
  - child closes socket immediately
  - child raises `SIGSTOP` without waiting for host ack

As a result, the stub/helper surface no longer needs:

- remote `read`
- host-side callback ack write

## Why This Was Worth Doing

This removes one more zygote-side helper dependency from the hot path while keeping the higher-level contract intact:

- host still learns child pid through the callback
- host still waits for stopped child before runtime delivery
- zygote restore still happens on the host side after callback handling

The tradeoff is explicit:

- we no longer use host ack as a synchronization point
- we now rely on "callback sent successfully, then child self-stops" instead

For the current architecture this is acceptable because the actual child runtime delivery already belongs to the host-side compat layer, not to the local stub.

## Verification

Minimal helper-surface test:

```powershell
g++ -std=c++17 -I . -I include -I src tests/headers/test_symbi_stub_minimal_helpers.cpp -o tests/headers/test_symbi_stub_minimal_helpers.exe
tests\headers\test_symbi_stub_minimal_helpers.exe
```

Host spawn regression:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector.exe
build\test_ninjector_spawn_injector.exe
```

Result:

- both verification steps passed

## Current Position

The remaining hot-path remote helpers are now:

- `getuid`
- `getpid`
- `socket`
- `connect`
- `write`
- `close`
- `raise`

This is a materially narrower remote dependency surface than the original stub contract.
