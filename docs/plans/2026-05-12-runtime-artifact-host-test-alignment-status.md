# 2026-05-12 Runtime Artifact Host Test Alignment Status

## Context

This follows `B2` in:

- [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md)

The runtime artifact policy unification landed first:

- [2026-05-12-runtime-artifact-policy-unification-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-runtime-artifact-policy-unification-status.md)

After that, the host `test_ninjector_spawn_injector` suite still encoded several old assumptions about spawn routing and fallback semantics.

## What Was Actually Fixed

One real logic fix was kept in production code:

- `ShouldUseEmbeddedSymbiAgent()` now returns `false` when `ops.spawn_symbi_embedded` is absent.
- This avoids selecting the embedded symbi path on host stubs that do not provide that callback, preventing `std::bad_function_call`.

Files:

- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

## What Was Test Debt, Not Runtime Regressions

The remaining failures were host-side expectation drift in:

- [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Aligned areas:

- zygote-control success still returns armed legacy-style sentinel pid `1`, not child pid
- default config with `enable_zygote_control = false` prefers symbi first
- legacy fallback remains allowed by current routing policy
- error strings now include fallback context when symbi fails and legacy fallback is attempted
- legacy tests using `__embedded_agent__` still need a usable `ncore` sidecar precondition when embedded legacy prepare fails
- tests that explicitly disable symbi must also establish the intended legacy fallback preconditions instead of assuming success

## Verification

Build:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector.exe
```

Run:

```powershell
build\test_ninjector_spawn_injector.exe
```

Result:

- host test executable completed successfully

## Current Position

`B2` is complete from the runtime artifact policy perspective, and the corresponding host test suite now matches current spawn semantics instead of older pre-unification expectations.

This keeps the code path stable for the next step in the existing plan, without silently changing backend policy just to satisfy outdated tests.
