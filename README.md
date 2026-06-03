# Nook

Nook is an Android dynamic instrumentation toolkit built around one shared hooking core. It provides three practical surfaces in the current repository:

- `libnook.so`: embedded framework for app-side or injector-side payloads
- `nook-server`: rooted device runtime for Frida-style `spawn` / `attach`
- `nook-gadget`: APK-patched in-app runtime for non-root packaging workflows

The current repository is organized as a reusable framework plus working runtime, tooling, and example payloads. The main supported development target is **arm64-v8a**.

## Framework Sample Workflows

### Mode 1: `nook-server` on a rooted device

Build the first server package from the repository root:

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
```

`nook-cli sodump` writes the raw mapped image plus a repaired ELF64 artifact that now synthesizes common dynamic sections for analysis.

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

Useful docs:

- [`docs/nook-gadget-usage.md`](./docs/nook-gadget-usage.md)
- [`docs/nook-dexdump-usage.md`](./docs/nook-dexdump-usage.md)
- [`docs/nook-sodump-usage.md`](./docs/nook-sodump-usage.md)

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

1. **App-side loading** via `System.loadLibrary(...)`
2. **Injector-side loading** via `Ninjector`

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

- `/data/local/tmp/Ninjector/libnook_native_verify_password_inline_test.so`

### Runtime files that must already exist

Under `/data/local/tmp/Ninjector/`:

- `libc++_shared.so`
- `libnook.so`
- `libnook_inline_observer_probe.so`
- the payload `.so` you want to inject

### Important behavior

- You do **not** inject `libnook.so` directly as the target payload.
- The payload loads `libnook.so` and the probe library as runtime dependencies.
- `libnook_inline_observer_probe.so` is required for deferred inline observer initialization, but is not injected as the main payload.

## Java Hook Example

A minimal macro-based Java Hook payload:

```cpp
#include "nook/NookJavaHookMacros.h"

NOOK_PAYLOAD_CONFIG("HOOK_EXAMPLE", 5, 200);

NOOK_JAVA_BLOCK(
    "com/demo/target/AdWallFragment",
    "loadAd",
    "(Ljava/lang/String;Ljava/lang/String;)V",
    0
);
```

Custom callback form:

```cpp
#include "nook/NookJavaHookMacros.h"

NOOK_PAYLOAD_CONFIG("HOOK_EXAMPLE", 5, 200);

NOOK_JAVA_HOOK(MyHook,
    "com/demo/target/Foo",
    "bar",
    "()V",
    0) {
    (void)env;
    (void)thiz;
    (void)args;
    (void)arg_count;
    (void)result;
    return 0;
}
```

Useful helper macros:

- `NOOK_PAYLOAD_CONFIG(tag, retry_count, retry_interval_ms)`
- `NOOK_JAVA_HOOK(name, class, method, sig, is_static)`
- `NOOK_JAVA_BLOCK(class, method, sig, is_static)`
- `NOOK_JAVA_REPLACE_BOOL(class, method, sig, is_static, value)`
- `NOOK_JAVA_REPLACE_INT(class, method, sig, is_static, value)`
- `NOOK_JAVA_REPLACE_LONG(class, method, sig, is_static, value)`
- `NOOK_JAVA_THIS_OBJECT(env, thiz)`
- `NOOK_JAVA_ARG_OBJECT(env, args, index)`

## Native Hook Examples

Current native examples:

- `examples/native_hook/nook_native_strcmp_test/`
  - Demonstrates native symbol interception for `strcmp`
- `examples/native_hook/nook_native_inline_test/`
  - Demonstrates inline hook replacement on a controlled sample target
- `examples/native_hook/nook_native_verify_password_inline_test/`
  - Demonstrates deferred inline hook installation for a target JNI symbol

Common sample-side runtime glue:

- [`examples/native_hook/common/nook_runtime_loader.h`](./examples/native_hook/common/nook_runtime_loader.h)

Important: `nook_runtime_loader.h` is **sample runtime glue**, not a stable framework contract.  
It currently reflects the injector-oriented workflow used in this repository.

## Native JS Bridge

Nook now exposes a first minimal JavaScript-facing native hook bridge inside the agent runtime:

```javascript
var result = Nook.Native.attach({
  type: "inline",
  module: "libnative-lib.so",
  symbol: "Java_com_demo_target_LoginFragment_verifyPasswordNative",
  onEnter: function (args) {},
  onLeave: function (retval) {},
});

var address = Module.findExportByName(
  "libnative-lib.so",
  "Java_com_demo_target_LoginFragment_verifyPasswordNative"
);

if (address !== null) {
  send(address.toString());
  send(address.add(4).toString());

  var listener = Interceptor.attach(address, {
    onEnter: function (args) {
      send(String(args[0]));
      send(String(args[0].add(4)));
    },
    onLeave: function (retval) {
      send(String(retval));
      send(String(retval.isNull()));
    },
  });

  rpc.exports.unhook = function () {
    return listener.detach();
  };
}
```

Current semantics:

- only `type: "inline"` is implemented
- `Nook.Native.attach(...)` uses `module + symbol`
- `Module.findExportByName(moduleName, symbolName)` resolves exports in already loaded modules and returns `NativePointer | null`
- `ptr(value)` creates a `NativePointer`
- `NULL` is a zero `NativePointer`
- minimal `NativePointer` methods are available:
  - `toString()`
  - `add(offset)`
  - `sub(offset)`
  - `isNull()`
  - `readUtf8String(maxLength?)`
- `Interceptor.attach(address, callbacks)` installs an immediate address-based inline hook and accepts a `NativePointer` or pointer string
- the return object includes:
  - `ok`
  - `hookId`
  - `deferred`
  - `detach()`
- `Interceptor.detach(hookId)` and `Interceptor.detachAll()` are available
- `listener.detach()` works during script load, `rpc.exports` calls, `recv()` callbacks, and native hook callbacks
- `onEnter(args)` receives an array of `NativePointer`
- `onLeave(retval)` receives a `NativePointer`

`readUtf8String()` reads a C-style UTF-8 string from the current process. It is intended for real `char*` arguments such as `strcmp`, `open`, or `dlopen` paths. Do not use it directly on JNI `jstring` values; those are Java objects and must be decoded through JNI-specific helpers later.

Meaning of `deferred`:

- `deferred: false`
  - the target module was already loaded
  - the inline hook was installed immediately
- `deferred: true`
  - the target module was not loaded yet
  - Nook stores a pending native-js hook record
  - Nook starts the inline module observer asynchronously
  - the hook is installed automatically when the module is loaded later

This is the correct path for app JNI libraries that are only loaded by a specific page, fragment, or class initializer.

## Deferred Inline Hook Design

The current arm64 deferred inline path works like this:

1. Payload registers a pending inline hook request with `NookInlineHookSymbolDeferred`.
2. The pending request is stored until the target module is loaded.
3. The inline observer hooks linker `soinfo::call_constructors()`.
4. A dedicated probe library, `libnook_inline_observer_probe.so`, is used to discover `soinfo` offsets safely.
5. When the target module is observed, the pending hook is installed before the target function is used.

This is conceptually similar to the linker-monitor approach used by frameworks such as shadowhook, but implemented inside Nook's own codebase.

The Native JS bridge reuses the same observer as the module-load signal source, but keeps its own pending state inside `src/agent_runtime/`:

1. `Nook.Native.attach(...)` validates the JS request.
2. Nook tries to resolve the symbol only in already loaded modules.
3. If the module is not loaded, the bridge:
   - reserves one inline hook slot
   - stores one pending native-js hook record
   - starts the inline module observer asynchronously
   - returns `{ ok: true, hookId, deferred: true }`
4. When the linker observer later sees the target module load, it calls back into the native-js bridge.
5. The bridge resolves the now-loaded symbol address and installs the real inline hook.
6. Hook enter/leave events are queued and then dispatched into QuickJS on the runtime side.

Important:

- deferred native-js registration is only considered successful if observer startup also succeeds
- `libnook_inline_observer_probe.so` is still required for this path because the observer needs it to discover `soinfo` layout safely

## Native JS Smoke Flow

The current validated smoke target is:

- module: `libnative-lib.so`
- symbol: `Java_com_demo_target_LoginFragment_verifyPasswordNative`

Typical attach command:

```powershell
nook-cli attach com.demo.target -l .\host\nook-py\native_hook.js --wait --usb
```

Expected patterns:

- if the target module is already loaded:
  - `native-attach-ok:{"ok":true,"hookId":...,"deferred":false}`
  - then `enter:...`
  - then `leave:...`
- if the target module is not loaded yet:
  - `native-attach-ok:{"ok":true,"hookId":...,"deferred":true}`
  - later, after the page/class loads the module:
    - `enter:...`
    - `leave:...`

Frida-style immediate attach smoke:

```powershell
nook-cli repl attach com.demo.target -l .\host\nook-py\interceptor_hook.js --usb
```

Expected patterns when the target module is already loaded:

- `find-export:0x...`
- `interceptor-attach-ok:{"mode":"interceptor","ok":true,"hookId":...,"deferred":false}`
- button/action hits the target:
  - `enter:...`
  - `leave:...`
- runtime detach:
  - `/call unhook []`
  - `rpc call ok: method=unhook result=true`

If the module is not loaded when `interceptor_hook.js` starts, the sample falls back to the deferred `Nook.Native.attach(...)` path:

- `find-export:null`
- `interceptor-attach-ok:{"mode":"deferred-native","ok":true,"hookId":...,"deferred":true}`
- callbacks fire after the target module is loaded and the target function is hit

If `deferred:true` appears but no later callback is triggered, check:

- the target page really loads the module after attach
- `libnook_inline_observer_probe.so` exists on device beside the runtime
- the target symbol name actually matches the exported JNI symbol

## Verification

### Host-side header/build tests

Examples:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_android_payload_examples_link_nook.cpp -o .\tests\headers\test_android_payload_examples_link_nook.exe
.\tests\headers\test_android_payload_examples_link_nook.exe
```

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_inline_hook_deferred_no_immediate_install.cpp -o .\tests\headers\test_inline_hook_deferred_no_immediate_install.exe
.\tests\headers\test_inline_hook_deferred_no_immediate_install.exe
```

### Android build verification

```powershell
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk -j4
```

### Successful deferred inline install log pattern

For a working deferred inline install, the log should include signals like:

- `install call_constructors observer ...`
- `soinfo offsets ready ...`
- `pending install module=... status=0`
- replacement function log, for example:
  - `hooked verifyPasswordNative => JNI_TRUE`

## Current Limits

- arm32 inline hook is not implemented yet
- `NookNativeHook*` is not yet a true unified PLT+Inline facade
- sample runtime glue is still oriented toward the current `Ninjector` workflow
- external user onboarding is improving, but examples are still closer to research/development usage than polished SDK packaging

## Non-goals Right Now

- x86 / x86_64 inline support
- arm32 inline support
- hiding all workflow-specific details behind one fully automatic runtime loader
- replacing the current example payload structure with a fully packaged SDK template
