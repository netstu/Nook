# ClassFactory Factory New Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add minimal `Java.ClassFactory.get(loader).$new(className, ...args)` support that forwards constructor resolution and object wrapping through the selected loader.

**Architecture:** Implement this entirely in the JS bootstrap layer inside `js_runtime.cpp` by composing existing loader-aware `__nookJavaUseWithLoader(...)` and wrapper-level `$new(...)`. Validate with host-side regression tests first, then device smoke using the demo app's `TextFragment`.

**Tech Stack:** QuickJS bootstrap string, C++ host regression tests, Android smoke scripts.

---

### Task 1: Add failing tests for `cf.$new`

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

- Add one test asserting `typeof Java.ClassFactory.get(loader).$new === 'function'`
- Add one test asserting `cf.$new('com.demo.target.TextFragment')` resolves and invokes the constructor with `loader_handle == 0x1111`

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected: FAIL because `cf.$new` is missing.

### Task 2: Implement minimal bootstrap support

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\agent_runtime\js_runtime.cpp`

**Step 1: Write minimal implementation**

- In `Java.ClassFactory.get(loader)` returned object, add:
  - `$new(className, ...args)`
  - validate `className` is a string
  - build `classWrapper = __nookJavaUseWithLoader(className, loaderHandle)`
  - return `classWrapper.$new.apply(classWrapper, remainingArgs)`

**Step 2: Run test to verify it passes**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected: PASS.

### Task 3: Add device smoke coverage

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_class_factory_factory_new_smoke.js`

**Step 1: Write smoke script**

- Enumerate loaders and pick `PathClassLoader`
- Assert `typeof cf.$new === 'function'`
- Validate:
  - `cf.$new('com.demo.target.TextFragment')`
  - `cf.$new('com.demo.target.TextFragment', '()V')`

**Step 2: Build and push Android artifact**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
```

**Step 3: Validate on device**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_class_factory_factory_new_smoke.js --wait --usb
```

Expected:
- binding exists
- both constructor paths return `com.demo.target.TextFragment`
- returned wrappers stay bound to the selected loader
