# 2026-05-11 Symbi Embedded Agent Materialization Status

## Summary

Current `symbi spawn` no longer requires the user to manually push `/data/local/tmp/nook/libnook-agent.so`.

The server already carries an embedded agent blob and materializes it on demand before the `symbi` backend consumes the `so_path`.

What is still true is:

- this is not yet a Frida-style pure in-memory child delivery path
- `symbi` still consumes a filesystem `.so` path
- the server is the component that now owns creating and cleaning that temporary agent artifact

So the current state is:

- manual sidecar deployment requirement: removed for `symbi spawn`
- true single-binary runtime shape: not fully achieved yet

## What Was Verified

### Code path

`server_main.cpp` selects the embedded agent path when no explicit `NOOK_AGENT_PATH` is provided.

`NinjectorSpawnInjector::EnsureLegacyAgentReady()` materializes the embedded blob to the selected runtime path if the file is absent.

`NinjectorSpawnInjector::SpawnViaSymbi()` passes that resolved path into `ops_.spawn_symbi(...)`.

`NinjectorSpawnInjector::FinalizeSpawn()` cleans up the materialized agent artifact for the `symbi` backend.

### Tests

Added/updated coverage in [`tests/communication/test_ninjector_spawn_injector.cpp`](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestSpawnSymbiMaterializesEmbeddedAgentOnDemand()`
  - verifies the agent file does not exist before spawn
  - verifies spawn materializes the embedded agent blob
  - verifies `symbi` receives the resolved file path
  - verifies finalize removes the materialized artifact

Also aligned pid expectations across backend-specific tests:

- `zygote-control`: pid sentinel `1`
- `legacy ncore`: pid sentinel `1`
- `symbi`: child pid propagated from backend stub (`17001` in unit tests)

Local verification completed with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector.exe
build\test_ninjector_spawn_injector.exe
```

and `nook_server` Android build:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application_static.mk APP_ABI=arm64-v8a APP_MODULES=nook_server -j4
```

## Log Clarification

Updated the server log in [`server/server_main.cpp`](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_main.cpp):

- from: `materialization=deferred`
- to: `materialization=on-demand`

This is more accurate because the embedded blob is not merely a conceptual fallback. It is actually materialized by the server-side injector path when needed.

## Remaining Gap To Frida

Frida-like “only one server binary exists on disk, no child-side `.so` materialization path” is still not done.

To get there, the next step is:

1. change `symbi` from path-based `.so` handoff to fd/memfd-based delivery
2. keep the embedded blob inside `nook-server`
3. remove the temporary runtime `.so` file requirement entirely for `symbi`

That is the remaining work if the goal is strict Frida-style single-binary delivery semantics.
