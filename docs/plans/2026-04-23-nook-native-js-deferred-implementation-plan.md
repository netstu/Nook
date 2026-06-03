# Nook Native JS Deferred Hook Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `Nook.Native.attach({ type: "inline", module, symbol, ... })` succeed even when the target module is not loaded yet, then automatically install the hook after the module is loaded.

**Architecture:** Reuse the existing Android inline-hook module observer as the module-loaded signal source, but keep Native JS pending-hook state inside `src/agent_runtime/` instead of mixing JS bridge state into the native pending-inline registry. Immediate-install and deferred-install paths share the same slot/event-dispatch machinery.

**Tech Stack:** C++17, QuickJS, current `JsRuntime`, `NookInlineHook` framework, existing inline-hook module observer, Android NDK toolchain.

---

### Task 1: Add failing bridge tests for deferred registration

**Files:**
- Modify: `tests/communication/test_native_js_bridge.cpp`
- Modify: `src/agent_runtime/nook_native_js_bridge.h`

**Step 1: Write the failing test**

Add tests that:

- simulate `ResolveSymbolAddressInLoadedModule(...)` failure
- call `InstallNativeJsHook(...)`
- expect success instead of failure
- expect a pending record to exist for that `hook_id`
- expect the returned record to keep `hook_handle == nullptr`

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_native_js_bridge.cpp src/agent_runtime/nook_native_js_bridge.cpp -o build/test_native_js_bridge.exe
build\test_native_js_bridge.exe
```

Expected: FAIL because unresolved loaded-module address still returns an error.

**Step 3: Write minimal implementation**

In `src/agent_runtime/nook_native_js_bridge.h/.cpp` add:

- `NativeJsPendingHookRecord`
- pending registry storage
- test helpers to query/reset pending state

Update `InstallNativeJsHook(...)` so unresolved loaded-module address:

- allocates `hook_id`
- reserves the slot
- stores one pending record
- returns success with `hook_handle == nullptr`

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: PASS.

### Task 2: Add failing tests for deferred install on module-loaded notification

**Files:**
- Modify: `tests/communication/test_native_js_bridge.cpp`
- Modify: `src/agent_runtime/nook_native_js_bridge.h`
- Modify: `src/agent_runtime/nook_native_js_bridge.cpp`

**Step 1: Write the failing test**

Add tests that:

- first register one deferred native-js hook
- then simulate `OnModuleLoaded("libnative-lib.so")`
- fake the runtime address resolver and `NookInlineHookAddress(...)`
- expect:
  - pending record becomes installed
  - `hook_handle` becomes non-null
  - replacement thunk still produces enter/leave events

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_native_js_bridge.exe
```

Expected: FAIL because there is no Native JS bridge module-loaded entry point yet.

**Step 3: Write minimal implementation**

Add a bridge entry such as:

- `size_t NotifyNativeJsHookModuleLoaded(const char* module_path, std::string* error_message);`

Implementation responsibilities:

- match pending native-js records by module name
- resolve loaded-module symbol address
- call `NookInlineHookAddress(...)`
- activate the existing slot with original/hook_handle
- mark pending entry installed

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_native_js_bridge.exe
```

Expected: PASS.

### Task 3: Add failing tests for JS-visible deferred result shape

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add runtime tests that:

- use fake dependencies to force deferred registration
- call `Nook.Native.attach(...)`
- expect JS result:

```javascript
{ ok: true, hookId: 1, deferred: true }
```

and for immediate path:

```javascript
{ ok: true, hookId: 1, deferred: false }
```

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task6.exe
```

Expected: FAIL because current JS result does not expose deferred state.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- extend the native attach result object with `deferred`
- keep callback registration behavior unchanged

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: PASS.

### Task 4: Bridge the existing module observer to Native JS deferred hooks

**Files:**
- Modify: `src/native_hook/inline_hook/inline_hook_module_observer.cpp`
- Modify: `src/agent_runtime/nook_native_js_bridge.h`
- Modify: `src/agent_runtime/nook_native_js_bridge.cpp`

**Step 1: Add the smallest integration seam**

After `TryInstallPendingInlineHooksForModule(...)` runs in `NotifyModuleLoaded(...)`, also call the Native JS bridge module-loaded notifier.

Do not move observer ownership into agent runtime.

**Step 2: Build Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4 NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk
```

Expected: PASS.

**Step 3: Re-run focused host tests**

Run:

```powershell
build\test_native_js_bridge.exe
build\test_js_runtime_native_attach_task6.exe
build\test_js_runtime_rpc.exe
```

Expected: all PASS.

### Task 5: Device smoke validation for deferred behavior

**Files:**
- Modify: `host/nook-py/native_hook.js` only if smoke logging needs refresh
- Modify: `host/nook-py/README.md`

**Step 1: Push updated artifacts**

Run:

```powershell
adb push libs/arm64-v8a/libnook-agent.so /data/local/tmp/nook/
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/
adb push libs/arm64-v8a/libc++_shared.so /data/local/tmp/nook/
```

**Step 2: Start server**

Run:

```powershell
adb shell "su -c 'LD_LIBRARY_PATH=/data/local/tmp/nook /system/bin/linker64 /data/local/tmp/nook/nook-server'"
```

**Step 3: Attach before `LoginFragment` loads `libnative-lib.so`**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\native_hook.js --wait --usb
```

Expected:

- `script load ok`
- one `native-attach-ok:...deferred:true...`
- no crash

**Step 4: Navigate to `LoginFragment` and trigger login**

Expected:

- hook installs automatically after module load
- later `verifyPasswordNative()` emits:
  - `enter:...`
  - `leave:...`

**Step 5: Update docs**

Document:

- immediate path
- deferred path
- why app JNI libs may not exist until fragment/class initialization
