## Summary

This checkpoint continues zygote-control state-boundary convergence by centralizing recorder-state resolution and moving finalize fallback terminal formatting onto a local state snapshot.

## What Changed

- Added `ResolveCurrentZygoteControlState(...)` in [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp) and declared it in [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h).
- Replaced duplicated spawn-side recorder resolution logic with this helper when zygote-control aborts or falls back.
- Tightened `InferZygoteControlLifecycleStateFromError(...)` so aggregated install-hook timeout details like `install zygote fork hook failed: rpc timeout` resolve to `ready-wait` instead of the coarser `install-hook`.
- In `FinalizeSpawn()`, when zygote-control fallback teardown fails, the code now snapshots the current zygote-control state immediately into a local `finalize_zygote_control_state` and uses that for terminal formatting instead of re-reading recorder state later.

## Why

Before this step:

- spawn had repeated ad-hoc state resolution blocks
- finalize fallback still formatted terminal errors from late recorder reads

That shape left too much room for terminal state to drift away from the actual failure point of the current attempt.

## Tests

Added/updated white-box coverage in [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestResolveCurrentZygoteControlStatePrefersRecordedFailureState`
- `TestResolveCurrentZygoteControlStateFallsBackToLifecycleThenDetail`
- `TestFinalizeFallbackFormatsLocalZygoteControlStateInsteadOfGlobalState`

Verification:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_state_helper_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_state_helper_green.exe"
```

## Progress Impact

This is still prerequisite cleanup, not the agent-owned stable spawn body.

What is cleaner now:

- recorder state resolution has one server-side entry point
- spawn-side abort/fallback state capture is less duplicated
- finalize fallback terminal state is tied to the local failure snapshot instead of a later global read

What remains before real agent-owned stable spawn:

- continue reducing non-transactional recorder dependencies
- move from recorder-assisted ownership cleanup to an explicit spawn transaction/state object that the agent-owned path can drive directly
