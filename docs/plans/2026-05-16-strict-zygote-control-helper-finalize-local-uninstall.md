# Strict Zygote-Control Helper Finalize Local Uninstall

## Goal

Remove the unnecessary RPC uninstall tail from successful strict `zygote-control` runs.

The observed device symptom was:

- strict spawn succeeded
- hook worked
- finalize still spent about 5 seconds attempting:
  - `nook.spawn.uninstallForkHook`
- that RPC then failed with:
  - `zygote control-ready agent session not found pid=<zygote> process=zygote64`
- finalize only succeeded after falling back to local helper cleanup

That behavior was wasteful and made successful strict runs look unstable.

## Root cause

`FinalizeZygoteControlSpawn(...)` already distinguished helper-only local control through:

- `owned_transaction->helper_only_local_control`

But its helper-only finalize helper still did this:

1. try `uninstall_zygote_hook_` first
2. only then fall back to
   - `ninjector::UninstallEmbeddedZygoteControlHooksByPid(...)`

So even on the strict helper-only path, finalize still paid for an RPC uninstall attempt that
should never have been part of the route.

## Change

Updated [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
and [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp):

- added `NinjectorSpawnOps::uninstall_embedded_zygote_control_hooks`
- defaulted that operation to:
  - `ninjector::UninstallEmbeddedZygoteControlHooksByPid`
- changed helper-only uninstall helpers in both:
  - strict-route rollback
  - finalize
- helper-only path now goes directly to local embedded helper uninstall
- helper-only path no longer attempts `uninstall_zygote_hook_` first

This keeps strict helper-only teardown aligned with its actual ownership model:

- zygote helper is local-only control state
- cleanup is local-only helper uninstall
- no control-ready RPC dependency should remain in that finalize branch

## Regression coverage

Updated [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestSpawnStrictHelperLocalControlSkipsRpcInstallButStillUninstalls`

It now asserts:

- strict helper route still skips RPC install
- strict helper finalize also skips RPC uninstall
- finalize uses the injected local uninstall operation instead

## Verification

Passed:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector.exe
.\build\test_ninjector_spawn_injector.exe
```

Passed:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_single_server_package.ps1
```

Current packaged server:

- [nook-server](/E:/Learn/my_program/all_my_hook/kanxue/Nook/build/single-server-package/arm64-v8a/nook-server)
- sha256: `134857264605c1e22d34aa799b24c98e84f69ecde88a2f5d188b7abc8b726a8e`

## Why this matters

This is a narrow but important convergence step for strict `zygote-control`:

- successful helper-only strict spawn no longer depends on a disappearing zygote control-ready session
- teardown cost is reduced on the success path
- the finalize route is better aligned with the helper split architecture

## Next

Verify on device that successful strict runs no longer show:

- `stage=uninstall ... method=nook.spawn.uninstallForkHook`
- followed by a 5 second RPC timeout

If that is gone, continue narrowing the remaining strict-path lifecycle edges around:

1. repeated strict spawn/re-finalize loops
2. explicit teardown/stop-server behavior
3. final transition toward agent-owned stable spawn semantics
