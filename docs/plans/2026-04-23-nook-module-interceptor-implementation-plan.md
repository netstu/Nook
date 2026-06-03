# Nook Module + Interceptor Implementation Plan

**Goal:** Add `Module.findExportByName(moduleName, symbolName)` and `Interceptor.attach(address, { onEnter, onLeave })` on top of the current Native JS bridge.

**Architecture:** Reuse the existing `nook_native_js_bridge` slot/event/cleanup path. Extend the hook request model so it can represent either `module + symbol` or direct `target_address`, then add QuickJS bindings for `Module` and `Interceptor`.

### Task 1: Lock JS runtime API surface with failing tests

Files:

- `tests/communication/test_js_runtime_native_attach.cpp`
- `src/agent_runtime/js_runtime.cpp`

Add tests for:

- `Module.findExportByName`
- `Interceptor.attach`
- success path returns hex-string address and `deferred:false`
- miss path returns `null`

### Task 2: Extend native-js bridge to support address-based install

Files:

- `src/agent_runtime/nook_native_js_bridge.h`
- `src/agent_runtime/nook_native_js_bridge.cpp`
- `tests/communication/test_native_js_bridge.cpp`

Add:

- request fields for direct target address
- install path that calls `NookInlineHookAddress(...)` directly
- bridge tests for address install and cleanup

### Task 3: Add QuickJS bindings

Files:

- `src/agent_runtime/js_runtime.cpp`

Add:

- `Module.findExportByName(moduleName, symbolName)`
- `Interceptor.attach(address, callbacks)`

Keep:

- `Nook.Native.attach(...)` unchanged

### Task 4: Run focused verification

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_native_js_bridge.cpp src/agent_runtime/nook_native_js_bridge.cpp -o build/test_native_js_bridge_task10.exe
build\test_native_js_bridge_task10.exe
```

```powershell
g++ ... tests/communication/test_js_runtime_native_attach.cpp ... -o build/test_js_runtime_native_attach_task10.exe
build\test_js_runtime_native_attach_task10.exe
```

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4 NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk
```

### Task 5: Device smoke

Use one script like:

```javascript
var target = Module.findExportByName("libnative-lib.so", "Java_com_demo_target_LoginFragment_verifyPasswordNative");
send({ type: "send", payload: "target=" + target });
Interceptor.attach(target, {
  onEnter(args) { send({ type: "send", payload: "enter:" + args[0] }); },
  onLeave(retval) { send({ type: "send", payload: "leave:" + retval }); },
});
```

Validate:

- attach succeeds when module already loaded
- callbacks fire
- unload/load still does not duplicate hooks
