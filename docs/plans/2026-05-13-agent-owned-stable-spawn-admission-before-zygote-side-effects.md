# Agent-Owned Stable Spawn Admission Before Zygote Side Effects

## Context

After the host-side orchestration had been substantially cleaned up, there was
still one ordering bug in `Spawn()` that directly affected zygote-control
stability:

- `BuildSpawnExecutionState()` ran before the admission gate
- `BuildSpawnExecutionState()` may call `TrySpawnViaZygoteControl()`
- therefore zygote-control side effects could happen even when the spawn should
  have been rejected immediately due to an already-active owner

This was no longer a stylistic issue. It was a real behavioral bug with direct
impact on device-side stability.

## Change

Moved the admission gate ahead of execution-state construction in `Spawn()`:

1. validate request
2. admit spawn request
3. build spawn execution state
4. apply routing
5. complete spawn orchestration

This guarantees that an already-active owner blocks the spawn before any
zygote-control attempt or install side effect can occur.

## Why It Matters

This is the first change in this stage that directly closes a stability hole for
the eventual `zygote-control` device path.

The earlier orchestration refactors were necessary because they made this bug
easy to see and isolate. Now that the top-level structure is cleaner, ordering
bugs like this are much easier to reason about and fix correctly.

This is a concrete sign that the host-side `agent-owned stable spawn` work is
starting to pay off toward the real target: reconnecting the cleaned-up host
path with reliable zygote-control behavior on device.

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
  -o build/test_ninjector_spawn_injector_admission_order_red.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_admission_order_red.exe"
```

Observed failure:

- `install_called` was true even though the spawn was rejected as
  `spawn already active for identifier`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_admission_order_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_admission_order_green.exe"
```
