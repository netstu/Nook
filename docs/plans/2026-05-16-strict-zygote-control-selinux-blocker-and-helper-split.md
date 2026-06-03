## Summary

Strict `zygote-control` on the current Android 11 / MIUI real device is blocked at the
first zygote-side load step for a structural reason, not a missing-file bug:

- `zygote64` cannot search `/data/local/tmp`
- loading `/data/local/tmp/nook/libnook-agent.so` from inside zygote fails before full
  agent initialization starts
- continued iteration on sidecar path loading is not a productive direction

At the same time, the codebase now has a concrete landing zone for a Frida-aligned
"minimal zygote helper" path:

- a new Android build target `nook_zygote_helper`
- installed artifact: `libs/arm64-v8a/libnook-zygote-helper.so`
- current size on this build: `945040`

This moves the project from "discussed split" to "buildable split scaffold".

## Real-device evidence

### Repro command

```powershell
python -m nook.cli -U -f com.ad2001.frida0x1 --strict-zygote-control -l E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\Test_Lab\nook-frida-labs\frida-0x1\script.js
```

### Observed failure

CLI:

```text
[-] spawn agent-ready failed for 'com.ad2001.frida0x1': zygote-control stage=spawn class=hard state=inject-agent detail=inject zygote agent failed: remote_dlopen_failed:dlopen failed: library "/data/local/tmp/nook/libnook-agent.so" not found
```

Device state at the same time:

- `/data/local/tmp/nook/libnook-agent.so` exists
- mode and size are correct
- runtime dir is present

### Root-cause evidence

Device context:

- `getenforce` => `Enforcing`
- zygote context => `u:r:zygote:s0`
- `/data/local/tmp` and `/data/local/tmp/nook` => `u:object_r:shell_data_file:s0`

Relevant AVC from logcat:

```text
avc: denied { search } for name="tmp" ... scontext=u:r:zygote:s0 tcontext=u:object_r:shell_data_file:s0 tclass=dir permissive=0
```

Correlated injector logs:

- `InjectSoHandleByPid: remote open failed pid=14753 so=/data/local/tmp/nook/libnook-agent.so`
- then `remote_dlopen_failed:dlopen failed: library "/data/local/tmp/nook/libnook-agent.so" not found`

Conclusion:

- this is not a "file materialization failed" problem
- this is not an `open()` vs `openat()` calling convention problem
- this is not currently a runtime-ready / spawn-response control-plane problem
- this is a zygote SELinux visibility problem for `/data/local/tmp`

## Code changes in this step

### 1. Better strict-path diagnostics

`server/ninjector_compat.cpp`

- added `IsShellTmpRuntimePath(...)`
- when remote open fails in a zygote-family process for `/data/local/tmp/...`,
  set a more specific inject error:
  - `remote_open_failed:zygote_shell_tmp_search_denied`

This is not the final solution. It prevents future debugging loops from treating the
failure as a generic `dlopen` issue.

### 2. Buildable zygote-helper split scaffold

`build/android/Android.mk`

- added `NOOK_ZYGOTE_HELPER_SRC`
- added module:
  - `LOCAL_MODULE := nook_zygote_helper`
  - `LOCAL_MODULE_FILENAME := libnook-zygote-helper`
  - `-DNOOK_ZYGOTE_HELPER_ONLY=1`

`src/framework/NookComm.cpp`

- made `nook_script_runtime_bridge.h` optional under `NOOK_ZYGOTE_HELPER_ONLY`
- `EnsureRuntimeBridgeAndReady()` now short-circuits with:
  - log: `runtime bridge unavailable in zygote-helper-only build process=%s`
  - return: `NOOK_STATUS_NOT_IMPLEMENTED`

`src/native_hook/inline_hook/inline_hook_module_observer.cpp`

- disabled `agent_runtime::NotifyNativeJsHookModuleLoaded(...)` under
  `NOOK_ZYGOTE_HELPER_ONLY`

### 3. Regression coverage

Added:

- `tests/headers/test_zygote_helper_split_scaffold.cpp`

This locks in:

- helper-specific Android.mk entries
- helper-only runtime bridge compile-time guard in `NookComm.cpp`

## Verification

### File-level regression

Passed:

```powershell
E:\MinGW\ucrt64\bin\g++.exe -std=c++17 -O2 -I. .\tests\headers\test_zygote_helper_split_scaffold.cpp -o .\build\test_zygote_helper_split_scaffold.exe
.\build\test_zygote_helper_split_scaffold.exe
```

### Android build

Passed:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application_static.mk APP_ABI=arm64-v8a APP_MODULES=nook_zygote_helper -j4
```

Result:

- `libs/arm64-v8a/libnook-zygote-helper.so`

## Why this matters

This is the first concrete step toward the previously discussed "agent-owned stable spawn"
direction on Android:

- zygote no longer needs to be treated as a place where the full JS/runtime-heavy agent must
  be loaded first
- helper-only control-plane logic now has a dedicated build target
- strict `zygote-control` can move toward:
  1. inject minimal helper into zygote
  2. install fork/specialize monitoring and control-plane RPC there
  3. let the app child load or activate the full agent later on the normal runtime path

That is materially closer to Frida's split between zygote gating/control responsibilities and
full runtime initialization.

## Next

1. Switch strict `zygote-control` zygote injection target from full embedded agent to
   `libnook-zygote-helper.so`
2. Keep current non-strict stable path unchanged while doing that
3. Once strict helper injection is working, move child-side full-agent activation back onto the
   existing stable runtime path instead of trying to boot the whole agent inside zygote

## Follow-up implementation status

The strict-route helper split has now been wired through the actual server build and packaging
path.

### Completed after this document was written

- `server/ninjector_spawn_injector.cpp`
  - strict `zygote-control` route now switches the zygote injection argument to the dedicated
    helper sentinel `__embedded_zygote_helper__`
  - fixed a bad strict-mode gate in `TrySpawnViaZygoteControl(...)`:
    - the code was incorrectly reading a nonexistent local `state.policy.strict_zygote_control`
    - it now derives strict mode directly from the request / environment in the zygote-control
      path, so the helper branch is actually reachable in the built server

- `server/ninjector_compat.cpp`
  - embedded helper injection remains backed by:
    - memfd name: `libnook-zygote-helper`
    - init symbol: `NookAgentInitializeForZygoteControl`

- `tools/build_single_server_package.ps1`
  - now rebuilds and refreshes all three embedded blobs in the required order:
    1. `libnook-agent.so`
    2. `libncore.so`
    3. `libnook-zygote-helper.so`
    4. then rebuilds `nook-server`

### Verification completed

- header regression tests passed:
  - `tests/headers/test_zygote_helper_split_scaffold.cpp`
  - `tests/headers/test_strict_zygote_control_uses_helper.cpp`

- Android builds passed:
  - `APP_MODULES=nook_zygote_helper`
  - `APP_MODULES=nook_server`

- single-server package rebuilt successfully:
  - packaged file: `build/single-server-package/arm64-v8a/nook-server`

### Device deployment note

On the current device, directly pushing to root-created `/data/local/tmp/nook/` is not reliable
from the shell user. The stable deployment pattern remains:

1. `adb push` to `/data/local/tmp/nook-server.tmp`
2. `su -c 'cp /data/local/tmp/nook-server.tmp /data/local/tmp/nook/nook-server'`
3. verify with `sha256sum` and `wc -c`

Verified packaged server on device:

- path: `/data/local/tmp/nook/nook-server`
- sha256: `bf1eeffbbd08d1b83d1647978ab1cb600a83a05aa132b8eaa2345a343b21cadd`
- size: `7131144`

At this point the remaining work is no longer "finish helper split wiring"; it is runtime
validation of the strict path on device and then continuing the child-side full-agent activation
split.
