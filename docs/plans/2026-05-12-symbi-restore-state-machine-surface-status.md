# 2026-05-12 Symbi Restore State Machine Surface Status

## Context

This is the first `A3` pass from:

- [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md)

The purpose of this pass was not to change restore primitives. It was to make the handoff/restore flow auditable as an explicit state progression.

## What Changed

Files:

- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)
- [test_symbi_restore_state_machine_surface.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_symbi_restore_state_machine_surface.cpp)

Added explicit handoff state surface:

- `enum class SymbiHandoffState`
- `SymbiHandoffStateName()`
- `AdvanceSymbiHandoffState()`

Current explicit states:

- `kGateInstalled`
- `kTargetAppStarted`
- `kCallbackObserved`
- `kPrimaryRestoreAttempted`
- `kPtraceRestoreAttempted`
- `kRestoreCompleted`

Applied state progression to:

- `complete_child_delivery_handoff()`
- `restore_with_fallback()`

This means the logs now reflect the intended flow directly instead of requiring the reader to infer phase transitions from scattered restore/helper messages.

## What Did Not Change

This pass did not change:

- restore success/failure criteria
- `SIGSTOP` restore path behavior
- ptrace fallback behavior
- callback timeout behavior
- spawn routing behavior

It is a readability/auditability pass over the existing restore logic.

## Verification

State-machine surface test:

```powershell
g++ -std=c++17 -I . -I include -I src tests/headers/test_symbi_restore_state_machine_surface.cpp -o tests/headers/test_symbi_restore_state_machine_surface.exe
tests\headers\test_symbi_restore_state_machine_surface.exe
```

Helper-surface regression:

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

- all verification steps passed

## Current Position

The symbi restore flow now reads more like a state machine, but the restore internals are still split across:

- `restore_original_slot()`
- `restore_original_slot_ptrace()`
- `restore_with_fallback()`
- `complete_child_delivery_handoff()`

So the next `A3` pass should target the remaining duplication in restore entry/exit handling, not the external spawn contract.
