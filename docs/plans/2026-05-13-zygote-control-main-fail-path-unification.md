## Summary

This checkpoint extends the use of `FailZygoteControlSpawn(...)` so that most of the main early-failure exits in `TrySpawnViaZygoteControl()` now go through one structured helper path.

## What Changed

- Switched additional `TrySpawnViaZygoteControl()` failure exits to `FailZygoteControlSpawn(...)`, including:
  - `zygote monitor ops incomplete`
  - launch failure with rollback error
  - launch failure without rollback error
- Together with the previous checkpoint, the main required-target failure exits now use one consistent path for:
  - transaction snapshot
  - error propagation
  - boolean failure return

## Why

Before this step, some failure branches had already moved to the helper, but a few central early-return exits still performed their own ad-hoc `SetError(...); return false;` flow.

That inconsistency made the failure path harder to reason about and kept part of the zygote-control transaction/writeback behavior fragmented.

After this step, the main spawn-path failures are substantially more uniform.

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_fail_helper_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_fail_helper_green.exe"
```

## Progress Impact

This is still not the final structured result builder, but it is very close to the point where `TrySpawnViaZygoteControl()` failure handling can be reasoned about as one pipeline instead of many local exits.

The next step is to finish collapsing any remaining stragglers and then consider whether the helper should evolve into an explicit failure-result constructor that writes directly into `SpawnOutcome`.
