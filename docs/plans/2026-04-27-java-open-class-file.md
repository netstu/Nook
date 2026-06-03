# Java Open Class File Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal `Java.openClassFile(path).load()` compatible path that creates a `DexClassLoader` and makes subsequent default `Java.use(...)` resolve through it.

**Architecture:** Implement this first in the QuickJS bootstrap layer by composing existing `Java.use`, `Java.setClassLoader`, and constructor invocation support. Avoid a new native bridge until the minimal Frida-style workflow is proven on host tests and device smoke.

**Tech Stack:** QuickJS bootstrap in `js_runtime.cpp`, host regression tests in `test_js_runtime_native_attach.cpp`, Android smoke script.

---

### Task 1: Write the failing tests

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

- Add a binding test for `typeof Java.openClassFile`
- Add a behavior test for:
  - `Java.openClassFile('/data/app/com.demo.injected/base.apk').load()`
  - `Java.use('com.demo.injected.Payload')`
  - default loader handle must become the new `DexClassLoader`

**Step 2: Run test to verify it fails**

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected: FAIL because `Java.openClassFile` is missing.

### Task 2: Implement minimal bootstrap support

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\agent_runtime\js_runtime.cpp`

**Step 1: Write minimal implementation**

- Add `Java.openClassFile(path)`
- Validate `path` is a string
- Return an object exposing `load()`
- `load()` should:
  - get current `Application` from `android.app.ActivityThread.currentApplication()`
  - get code cache directory from `app.getCodeCacheDir().getAbsolutePath()`
  - get parent loader from `app.getClassLoader()`
  - construct `dalvik.system.DexClassLoader`
  - use exact constructor signature to avoid current overload resolver superclass mismatch
  - call `Java.setClassLoader(loader)`
  - return the loader wrapper

**Step 2: Run test to verify it passes**

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected: PASS.

### Task 3: Add device smoke and validate

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_open_class_file_smoke.js`

**Step 1: Write smoke script**

- Inside `Java.ready(...)`:
  - obtain current application
  - use `app.getPackageCodePath()` as the input path
  - call `Java.openClassFile(apkPath).load()`
  - validate loader wrapper class name
  - validate subsequent `Java.use('com.demo.target.TextFragment')` is bound to that loader
  - validate `TextFragment.$new("()V")` still works

**Step 2: Build and push Android artifact**

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
```

**Step 3: Validate on device**

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_open_class_file_smoke.js --wait --usb
```

Expected:
- `Java.openClassFile` binding exists
- `load()` returns a `DexClassLoader`
- subsequent default `Java.use(...)` resolves through the returned loader
