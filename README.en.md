# Nook

Nook is an Android dynamic instrumentation toolkit built around one shared hooking core. It provides three practical surfaces in the current repository:

- `libnook.so`: embedded framework for app-side or injector-side payloads
- `nook-server`: rooted device runtime for Frida-style `spawn` / `attach`
- `nook-gadget`: APK-patched in-app runtime for non-root packaging workflows

The current repository supports the `nook-gadget` APK patch / listen / startup-script workflow.  
The host CLI also supports `dexdump`, so you can dump in-memory Dex artifacts directly from a live target process.

The current repository is organized as a reusable framework plus working runtime, tooling, and example payloads. The main supported development target is **arm64-v8a**.

## Framework Sample Workflows

### Mode 1: `nook-server` on a rooted device

Build the server package from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_single_server_package.ps1 -ForceRebuild
```

Expected server artifact:

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

Then use the host CLI:

```powershell
nook-cli -U -f com.demo.target -l .\hook.js
nook-cli -U com.demo.target -l .\hook.js
nook-cli sodump com.demo.target -U --module libfoo.so
nook-cli dexdump com.demo.target -U
```

`nook-cli sodump` writes the raw mapped image plus a repaired ELF64 artifact for follow-up analysis.

Without `--gadget`, `nook-cli -U ...` assumes a running rooted `nook-server`.

### Mode 2: `nook-gadget` patched into an APK

Build the gadget library:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_nook_gadget.ps1 -ForceRebuild
```

Patch an APK and package a startup script:

```powershell
nook-cli patchapk .\target.apk -s .\startup.js
```

Or use the shorter helper command:

```powershell
nook-gadget patchapk --source .\target.apk --startup-script .\startup.js
```

Patch an APK in `listen` mode and pause app startup until the host attaches:

```powershell
nook-cli patchapk .\target.apk --on-load wait
```

Attach to a gadgetized target from the host:

```powershell
nook-cli -U --gadget com.demo.target -l .\hook.js
nook-cli -U --gadget com.demo.target
nook-cli sodump -U --gadget com.demo.target --module libfoo.so
nook-cli dexdump -U --gadget com.demo.target
```

Important gadget semantics:

- `--gadget` explicitly switches the host attach path to the gadget `listen` socket
- `--on-load resume` is the default gadget behavior
- `--on-load wait` pauses until the host loads a script and resumes the target process
- packaged startup-script mode and host-attached `listen` mode are both supported

## Current Support Matrix

| Feature | Status | Notes |
| --- | --- | --- |
| Java Hook | Supported | Built into `libnook.so` |
| PLT Hook | Supported | ELFIO-first implementation with fallback parser |
| Inline Hook (arm64) | Supported for current workflow | Direct address hook and deferred symbol hook are available |
| Inline Hook (arm32) | Not supported yet | Planned later |
| App-side `System.loadLibrary(...)` workflow | Supported | Best for app-integrated testing |
| Injector / remote `dlopen(...)` workflow | Supported | Current examples assume `Ninjector`-style runtime placement |

## Repository Layout

```text
include/nook/                 public headers
src/framework/                public API entrypoints
src/java_hook/                Java Hook implementation
src/native_hook/core/         shared native-hook helpers
src/native_hook/plt_hook/     PLT hook implementation
src/native_hook/inline_hook/  arm64 inline hook implementation
src/common/                   shared utilities
examples/java_hook/           Java Hook payload examples
examples/native_hook/         native payload examples
build/android/                Android.mk, Application.mk, CMakeLists.txt
tests/headers/                host-side verification tests
third_party/                  embedded third-party code
```

Additional runtime/tooling paths:

```text
host/nook-py/                 Python CLI and gadget helper
server/                       nook-server sources
src/gadget/                   nook-gadget runtime
tools/                        build, patch, and validation scripts
```

## Public Headers

- [`include/nook/Nook.h`](./include/nook/Nook.h)
- [`include/nook/NookJavaHook.h`](./include/nook/NookJavaHook.h)
- [`include/nook/NookJavaHookMacros.h`](./include/nook/NookJavaHookMacros.h)
- [`include/nook/NookPltHook.h`](./include/nook/NookPltHook.h)
- [`include/nook/NookInlineHook.h`](./include/nook/NookInlineHook.h)
- [`include/nook/NookNativeHook.h`](./include/nook/NookNativeHook.h)

## API Overview

### Java Hook

- `NookJavaHookInitialize`
- `NookJavaHookHook`
- `NookJavaHookUnhook`
- `NookJavaHookUnhookAll`

Recommended payload style: use the macros in `NookJavaHookMacros.h`.

### PLT Hook

- `NookPltHookInitialize`
- `NookPltHookSymbol`

Use this when the target function is imported through PLT/GOT relocation.

### Inline Hook

- `NookInlineHookInitialize`
- `NookInlineHookAddress`
- `NookInlineHookSymbol`
- `NookInlineHookSymbolDeferred`
- `NookInlineUnhook`

Use `NookInlineHookSymbolDeferred` when the target module may not be loaded yet.

### Native Hook Facade

- `NookNativeHookInitialize`
- `NookNativeHookHookSymbol`

Important: **`NookNativeHook*` currently routes to the PLT hook path only.**  
If you want inline behavior, call `NookInlineHook*` directly.

## Build

### Requirements

- Android NDK with `ndk-build`
- Windows PowerShell or any shell that can invoke `ndk-build`

Current Android build configuration:

- ABI: `arm64-v8a`
- Platform: `android-30`
- STL: `c++_shared`

See [`build/android/Application.mk`](./build/android/Application.mk).

### Build Command

Run from the repository root:

```powershell
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk -j4
```

If `ndk-build` is not in `PATH`, use your local NDK path explicitly, for example:

```powershell
& "$env:ANDROID_NDK_HOME\ndk-build.cmd" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk -j4
```

### Main Output Artifacts

Generated under `libs/arm64-v8a/`:

- `libnook.so`
- `libnook_inline_observer_probe.so`
- `libnook_java_hook_example.so`
- `libnook_native_strcmp_test.so`
- `libnook_native_inline_test.so`
- `libnook_native_verify_password_inline_test.so`

## Quick Start

There are two practical usage modes in the current repository:

1. app-side loading via `System.loadLibrary(...)`
2. injector-side loading via `Ninjector`

The example payloads in `examples/native_hook/` are sample payloads, not the framework itself.

## Workflow 1: App-side `System.loadLibrary(...)`

This is the easiest way to test a payload inside an APK you control.

### Java-side load order

For the inline verify-password sample, load:

```java
System.loadLibrary("c++_shared");
System.loadLibrary("nook");
System.loadLibrary("nook_native_verify_password_inline_test");
```

Do **not** manually load:

- `libnook_inline_observer_probe.so`

The probe library is loaded internally by the deferred inline observer when needed.

### Notes

- If your target module is loaded later, use `NookInlineHookSymbolDeferred`.
- In the demo project, `libnative-lib.so` is loaded by the target fragment itself, so deferred installation is the correct path.

## Workflow 2: Injector-side loading with `Ninjector`

In this workflow, you inject only the payload `.so`, but the payload still needs its runtime dependencies present on device.

### Injected payload

For the verify-password inline sample:

- inject: `libnook_native_verify_password_inline_test.so`

### Required runtime dependencies on device

- `libc++_shared.so`
- `libnook.so`
- `libnook_inline_observer_probe.so`

How you place these dependencies depends on your injector and device environment.

## Notes

- Current primary target: Android `arm64-v8a`
- The public repository is kept code-focused by default and does not include local validation docs or release-prep artifacts
- `nook-server`, `nook-gadget`, `dexdump`, and `sodump` all build on the same shared core
