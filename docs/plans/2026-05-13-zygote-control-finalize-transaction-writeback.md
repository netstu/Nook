## Summary

This checkpoint completes the first transactional write-back path for zygote-control by making finalize teardown update the transaction object in-place.

## What Changed

- Changed [FinalizeZygoteControlSpawn()](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp) to take `ZygoteControlOwnedTransaction*` instead of a by-value transaction copy.
- On finalize teardown:
  - entering clear phase now updates `transaction->lifecycle_state`
  - clear failure now updates both `transaction->failure_state` and `transaction->lifecycle_state` to `kFinalizeClear`
  - success clears the transaction-carried state back to `kUnknown`
- Updated both finalize call sites to pass a transaction pointer:
  - owned zygote-control finalize path
  - fallback zygote-control finalize probe path

## Why

Before this change:

- finalize could read transaction-carried state
- but finalize could not write its own failure back into that transaction

That meant finalize still depended on recorder globals to reflect the latest teardown failure. This blocked the transition toward a true transaction-owned state model.

After this change:

- finalize is no longer just a consumer of transaction state
- it also becomes a producer of transaction state

This is necessary groundwork for a future spawn/result transaction object that remains authoritative across setup, handoff, and teardown.

## Tests

Added regression in [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestFinalizeZygoteControlSpawnWritesStateBackToTransaction`

Verification:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_finalize_txn_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_finalize_txn_green.exe"
```

## Progress Impact

This is still not the complete agent-owned stable spawn implementation.

But it matters because zygote-control transactions now support both:

- state seeding during successful spawn setup
- state write-back during finalize teardown

The next real step is to stop treating failure paths as ad-hoc string/error side effects and instead commit them into a structured spawn/result transaction object.
