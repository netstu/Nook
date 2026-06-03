# 2026-05-12 Symbi Restore Driver Unification Status

## Context

This is the second `A3` pass after:

- [2026-05-12-symbi-restore-state-machine-surface-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-symbi-restore-state-machine-surface-status.md)

The first pass made the handoff flow auditable as explicit state progression.

This pass targeted the remaining duplication inside the restore primitives themselves.

## What Changed

Files:

- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)
- [test_symbi_restore_state_machine_surface.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_symbi_restore_state_machine_surface.cpp)

Added shared restore driver structure:

- `struct RestoreDriverOps`
- `run_restore_attempt()`

Split primitive-specific entry/exit into small helpers:

- primary path:
  - `stop_restore_target()`
  - `resume_restore_target()`
- ptrace path:
  - `ptrace_attach_restore_target()`
  - `ptrace_detach_restore_target()`

After this change:

- `restore_original_slot()` is now just a configured primary driver call
- `restore_original_slot_ptrace()` is now just a configured ptrace driver call plus the existing critical-failure log

## Why This Matters

Before this pass, both restore primitives repeated the same high-level shape:

1. acquire stopped state
2. run `restore_write_mem()`
3. leave stopped state
4. log success/failure

That made it harder to audit whether the two restore paths stayed behaviorally aligned.

Now the shared skeleton lives in one place, and the path-specific pieces are explicit small helpers.

## What Did Not Change

This pass did not change:

- restore failure enums
- restore stage names
- ptrace fallback policy
- callback timeout policy
- spawn routing behavior

It is still a structural cleanup, not a semantic policy change.

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

`A3` is now materially cleaner:

- handoff states are explicit
- restore driver skeleton is shared
- primary and ptrace paths differ only in the small entry/exit hooks and the existing ptrace critical log

If more work is done here later, it should focus on log wording and failure aggregation detail, not on further structural breakup of the current restore core.
