# Nook

Nook is an Android dynamic instrumentation toolkit built on one shared hooking core.

This repository currently exposes three practical surfaces:

- `libnook.so`: embedded framework for app-side or injector-side payloads
- `nook-server`: rooted device runtime for Frida-style `spawn` / `attach`
- `nook-gadget`: APK-patched in-app runtime for gadget workflows

The main public target is **arm64-v8a** on Android.

## Quick Start

### 1. Rooted `nook-server` workflow

Build the server package from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_single_server_package.ps1 -ForceRebuild
```

Expected output:

```text
build/single-server-package/arm64-v8a/nook-server
```

Deploy and start it on device:

```powershell
adb shell "su -c 'mkdir -p /data/local/tmp/nook'"
adb push .\build\single-server-package\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server'"
adb shell "su -c '/data/local/tmp/nook/nook-server'"
```

Common host commands:

```powershell
nook-cli -U -f com.demo.target -l .\hook.js
nook-cli -U com.demo.target -l .\hook.js
```

Without `--gadget`, `nook-cli -U ...` assumes a running rooted `nook-server`.

### 2. `nook-gadget` APK workflow

Build the gadget library:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_nook_gadget.ps1 -ForceRebuild
```

Patch an APK and package a startup script:

```powershell
nook-cli patchapk .\target.apk -s .\startup.js
```

Or use the short helper:

```powershell
nook-gadget patchapk --source .\target.apk --startup-script .\startup.js
```

Patch an APK in gadget `listen` mode and hold the process until the host attaches:

```powershell
nook-cli patchapk .\target.apk --on-load wait
```

Attach from the host to a gadgetized app:

```powershell
nook-cli -U --gadget com.demo.target -l .\hook.js
```

Important gadget semantics:

- `--gadget` explicitly selects the gadget `listen` socket path
- `--on-load resume` is the default
- `--on-load wait` pauses the app until the host loads a script and resumes it
- both packaged startup-script mode and host-attached `listen` mode are supported

## Repository Layout

```text
build/android/      Android NDK build files
docs/               architecture, design, and validation notes
examples/           sample payloads and demos
host/nook-py/       Python CLI and gadget helpers
include/            public headers
server/             nook-server sources
src/                framework and runtime sources
tests/              unit and regression tests
third_party/        bundled dependencies
tools/              build, patch, and validation scripts
```

## Notes

- Current primary target: rooted Android arm64
- `nook-cli patchapk` and `nook-gadget patchapk` share the same patch engine
- `--gadget` is explicit by design; normal attach syntax still targets `nook-server`
