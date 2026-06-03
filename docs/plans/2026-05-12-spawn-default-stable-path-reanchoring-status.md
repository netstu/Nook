# 2026-05-12 Spawn Default Stable Path Reanchoring Status

## Context

This follows the earlier explicit-symbi work:

- [2026-05-12-spawn-symbi-explicit-flag-design.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-spawn-symbi-explicit-flag-design.md)
- [2026-05-12-spawn-backend-responsibility-clarification-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-spawn-backend-responsibility-clarification-status.md)
- [2026-05-12-symbi-convergence-phase-summary.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-symbi-convergence-phase-summary.md)

The code had drifted into:

- default path preferring `symbi` whenever `enable_zygote_control == false`

That no longer matched the intended stable-vs-experimental split.

## What Changed

Files:

- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Routing change:

- before:
  - default path preferred `symbi`
  - explicit `--nook-spawn-backend=symbi` also preferred `symbi`
- now:
  - default path stays on stable legacy ncore
  - explicit `--nook-spawn-backend=symbi` is the only route that prefers `symbi`
  - experimental `zygote-control` remains a separate path when enabled

Implementation detail:

- `should_try_symbi_first` now depends only on `explicit_symbi_requested`
- default logging now says stable legacy path is kept by default
- symbi call-site comments now describe it as an explicit experimental backend, not the default stable route

## Test Realignment

Host tests were updated to match the intended policy:

- default no longer expects `symbi`
- explicit symbi tests still cover:
  - symbi success
  - symbi fallback to legacy
  - symbi embedded/materialized agent paths
- legacy-default tests now supply the preconditions legacy actually needs:
  - available `ncore`
  - embedded agent sentinel where required

## Why This Matters

This re-establishes the separation that the design docs were aiming for:

- stable path = legacy ncore prepare/clear
- experimental path = explicit symbi
- separate experimental path = zygote-control

Without this, the code and docs were pulling in different directions.

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector.exe
build\test_ninjector_spawn_injector.exe
```

Result:

- host spawn test executable completed successfully

## Current Position

At this point the spawn policy is once again coherent at the code surface:

- default stable backend is legacy
- explicit symbi is explicit again
- symbi internal cleanup from `A1/A2/A3` remains useful, but no longer silently changes the default routing contract
