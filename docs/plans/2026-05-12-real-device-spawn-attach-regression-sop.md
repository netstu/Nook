# 2026-05-12 Real-Device Spawn/Attach Regression SOP

## Goal

Provide a clean, current regression checklist for the now-working Android 11 real-device validation path.

This SOP is intentionally narrow:

- current stable/default spawn behavior
- explicit `--spawn-symbi`
- attach sanity
- expected commands, outputs, and logs

It is not a historical design document.

## Current Stable Baseline

As of the latest validated state:

- default `spawn` stays on the stable legacy backend
- explicit `--spawn-symbi` is supported
- device-visible deployment only needs:
  - `nook-server`
- current runtime dir:
  - `/data/local/tmp/nook`
- tested target device:
  - Android 11 Xiaomi `cepheus`

## Roles

Assistant side:

- modify code
- build
- refresh embedded blobs when needed
- push artifacts
- provide test commands
- analyze logs

User side:

- start `nook-server`
- run `nook-cli` test commands
- report observed behavior
- send CLI output and device logs back

## When Re-Push Is Required

Re-push `nook-server` when:

- any `server/` runtime behavior changed
- embedded agent or embedded ncore content changed
- attach/spawn/session-state logic changed

No device push is needed when:

- only Python CLI changed
- only host-side tests changed
- only docs changed

## Build Commands

### Build `nook-server`

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a APP_MODULES=nook_server -j4
```

### Host-side regression checks commonly used

```powershell
python -m unittest host/nook-py/tests/test_cli.py
```

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers.exe
build\test_server_handlers.exe
```

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_host_spawn_client.cpp src/communication/host/host_spawn_client.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_host_spawn_client.exe
build\test_host_spawn_client.exe
```

## Device Prep

### Clean runtime dir

```powershell
adb shell "su -c 'rm -rf /data/local/tmp/nook && mkdir -p /data/local/tmp/nook && chmod 777 /data/local/tmp/nook'"
```

### Push server

```powershell
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
```

### Set permissions and verify

```powershell
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server && ls -l /data/local/tmp/nook'"
```

Expected stable visible state:

- only `nook-server` exists in `/data/local/tmp/nook`

## Start Server

```powershell
adb shell "su -c 'nohup /system/bin/linker64 /data/local/tmp/nook/nook-server >/data/local/tmp/nook/server.out 2>/data/local/tmp/nook/server.err < /dev/null &'"
```

## Core Real-Device Regression Matrix

### Case 1: Default spawn, script.js

```powershell
nook-cli -U -f com.ad2001.frida0x1 -l E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\Test_Lab\nook-frida-labs\frida-0x1\script.js
```

Expected:

- app launches normally
- hook is effective
- CLI shows:
  - `Spawning ...`
  - `Waiting for agent runtime ready...`
  - `Spawned (pid: ...)`
  - `Loading 'script.js'...`

### Case 2: Default spawn again, script.js

Run the same command again immediately.

Expected:

- second run also succeeds
- no stale state / timeout / wrong pid reuse

### Case 3: Explicit symbi, script.js

```powershell
nook-cli -U -f com.ad2001.frida0x1 --spawn-symbi -l E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\Test_Lab\nook-frida-labs\frida-0x1\script.js
```

Expected:

- hook is effective
- no fallback needed

### Case 4: Default spawn, script2.js

```powershell
nook-cli -U -f com.ad2001.frida0x1 -l E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\Test_Lab\nook-frida-labs\frida-0x1\script2.js
```

Expected:

- hook is effective
- log output appears from the script as expected

### Case 5: Attach sanity

Use the current attach test case you are validating for the target app.

Expected:

- attach succeeds
- no `agent runtime not ready for script create`
- app remains responsive

## Logs To Collect After Each Real-Device Run

### Runtime dir state

```powershell
adb shell "su -c 'cat /data/local/tmp/nook/server.err; echo ----; cat /data/local/tmp/nook/server.out; echo ----; ls -l /data/local/tmp/nook'"
```

Expected:

- no unexpected visible sidecar files
- ideally only `nook-server`

### Core server/injector logs

```powershell
adb logcat -d -s Ninjector NookServer NookComm NSymbiStub
```

### Expanded timing logs when investigating startup latency

```powershell
adb logcat -d -v time -s Ninjector NookServer NookComm NSymbiStub ActivityManager ActivityTaskManager
```

## What Good Logs Look Like

For default spawn:

- `default spawn keeps stable legacy path`
- `prepare_spawn_in_zygote ...`
- `spawn prepared via legacy ncore ...`
- `replay cached AGENT_READY`
- `forward SCRIPT_CREATE`
- `forward SCRIPT_LOAD_RESP`
- `resume success`

For explicit symbi:

- `explicit symbi spawn requested; skip zygote control`
- then the same successful symbi chain

## Known Acceptable Current Behavior

These are currently acceptable and not treated as failures:

- two agent socket connections for the same child pid
- first ready shape with `name=zygote64`
- second ready shape with actual target process name

Current intended semantics:

- control-stage ready = handoff/control signal
- runtime-stage ready = authoritative script-capable agent

## Known Failure Signals

Treat these as regressions:

- `spawn agent-ready timed out`
- `agent runtime not ready for script create`
- `script create timed out`
- unexpected symbi selection during stable default validation
- visible runtime sidecar files reappearing unexpectedly in `/data/local/tmp/nook`
- app white-screening indefinitely
- device reboot

## Recommended Validation Order For Future Work

After any spawn/session-state change:

1. local host/server regression tests
2. build `nook-server`
3. clean + push device runtime dir
4. default spawn once
5. default spawn second time
6. explicit `--spawn-symbi`
7. attach sanity

## Boundary

This SOP is only for the current stable validation surface.

It does not imply:

- `zygote-control` is production-ready
- the two-stage symbi connection model is fully eliminated
- all Android versions are supported the same way

It is the current practical truth for the tested device path.
