# spawn execution policy model

## Context

After route orchestration and terminal-tail extraction, the remaining outer shell still threaded several booleans independently:

- `explicit_symbi_requested`
- `strict_zygote_control`
- `allow_symbi_backend`
- `allow_legacy_backend_fallback`

These values were already conceptually one execution policy, but they were still passed around as separate scalars.

## Change

Introduced:

- `SpawnExecutionPolicy`
- `BuildSpawnExecutionPolicy(...)`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

`SpawnExecutionPolicy` currently carries:

- `explicit_symbi_requested`
- `should_try_symbi_first`
- `strict_zygote_control`
- `allow_symbi_backend`
- `allow_legacy_backend_fallback`

Integration changes:

- `ApplySpawnRoutingAttempts(...)` now consumes `SpawnExecutionPolicy`
- `ApplyTerminalSpawnOutcome(...)` now consumes `SpawnExecutionPolicy`
- `Spawn()` now builds one policy object and passes it through the outer flow

This removes another layer of scalar drift and makes the remaining shell closer to a real execution-state model.

## Tests

Added white-box regressions in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestBuildSpawnExecutionPolicyDefaultStablePath()`
- `TestBuildSpawnExecutionPolicyExplicitSymbiRequest()`

Also updated orchestration/tail helper tests to consume the policy object instead of standalone booleans.

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_policy_model_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_policy_model_green.exe"
```

## Why this matters

This is the first step that is not just “extract helper X,” but “introduce an explicit model object for the outer shell.”

That matters because the next phase toward `agent-owned stable spawn` should build on stable model objects, not more loose booleans. `SpawnExecutionPolicy` is now the natural place to grow:

- route preferences
- backend availability
- strictness / fallback contracts
- later terminal-resolution directives

In other words, the current cleanup track has now crossed from helper extraction into early state-model convergence.
