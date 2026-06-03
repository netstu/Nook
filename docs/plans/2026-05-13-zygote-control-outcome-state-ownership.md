## Summary

This checkpoint moves zygote-control terminal state ownership into the per-spawn `SpawnOutcome` model instead of reconstructing it late from global recorder state and error strings.

## What Changed

- Added `zygote_control_state` to `SpawnOutcome` in [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h).
- When zygote-control fails before fallback or abort is decided, spawn now snapshots the authoritative state into `outcome.zygote_control_state` in [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp).
- `ShouldAllowZygoteControlFallback()` now prefers `outcome.zygote_control_state` instead of re-reading global lifecycle/failure state.
- `FinalizeSpawnOutcome()` now formats terminal logs and errors from `outcome.zygote_control_state` instead of reconstructing state from recorder globals.

## Why

The previous shape still allowed late terminal classification to be influenced by recorder residue outside the immediate spawn outcome. That is the wrong state ownership model for the upcoming agent-owned stable spawn work.

The goal here is to make:

- fallback decision
- spawn terminal log
- spawn terminal error

all consume the same per-attempt state object.

## Tests

Added host-side white-box regressions in [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestSpawnOutcomeFallbackClassificationUsesOutcomeStateInsteadOfGlobalState`
- `TestSpawnOutcomeAbortFormatsOutcomeStateInsteadOfGlobalState`

Verification:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_state_model_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_state_model_green.exe"
```

## Progress Impact

This is still pre-work for real `agent-owned stable spawn`.

What is now cleaner:

- per-attempt zygote-control failure state is explicit
- spawn terminal behavior no longer depends on late global-state reconstruction

What still remains before the real implementation:

- reduce finalize-side reliance on recorder globals where possible
- move from current ownership cleanup into a real spawn transaction/state model for agent-owned orchestration
