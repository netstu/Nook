## Summary

This checkpoint starts moving zygote-control state ownership from global recorder-only tracking into the zygote-control transaction object itself.

## What Changed

- Extended [ZygoteControlOwnedTransaction](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h) with:
  - `failure_state`
  - `lifecycle_state`
- On successful zygote-control spawn setup, [TrySpawnViaZygoteControl()](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp) now seeds those transaction fields from the current recorder snapshot before the transaction is committed.
- Added `ResolveTransactionZygoteControlState(...)` in [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp), which prefers transaction-carried state and only falls back to recorder/detail inference when the transaction does not carry anything useful yet.
- Finalize fallback now uses `ResolveTransactionZygoteControlState(...)` for local zygote-control terminal state resolution.

## Why

Previous cleanup steps improved terminal formatting, but the underlying state still primarily lived in global recorder fields.

That is not enough for the eventual agent-owned stable spawn design, where:

- a concrete spawn transaction must carry its own state
- terminal decisions should be derived from that transaction
- recorder globals should become auxiliary rather than authoritative

This step seeds that transition without changing external behavior.

## Tests

Added white-box regressions in [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestResolveTransactionZygoteControlStatePrefersTransactionState`
- `TestSuccessfulZygoteControlSpawnCommitsTransactionState`

Verification:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_transaction_state_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_transaction_state_green.exe"
```

## Progress Impact

This is the first concrete step that starts to look like pre-transaction state ownership for the future agent-owned stable spawn path.

Still not done:

- failure paths are not yet uniformly committed into a first-class transaction/result model
- recorder state is still written broadly during zygote-control execution
- spawn ownership is still split between active state and recorder side channels

But compared with the previous state:

- zygote-control now has a transaction object that can carry state
- finalize fallback can prefer transaction-carried state over global recorder state
