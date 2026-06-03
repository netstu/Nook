# Java ClassFactory.openClassFile Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal Frida-style `Java.ClassFactory.get(loader).openClassFile(path).load()` path that is loader-scoped and does not mutate the global default loader.

**Architecture:** Implement the feature entirely in the QuickJS bootstrap by extending the object returned from `Java.ClassFactory.get(loader)`. Reuse the existing `DexClassLoader` construction path already used by `Java.openClassFile(...)`, but switch parent-loader selection to the current factory loader and avoid `Java.setClassLoader(...)`.

**Tech Stack:** QuickJS bootstrap in `src/agent_runtime/js_runtime.cpp`, host regression tests in `tests/communication/test_js_runtime_native_attach.cpp`, Android smoke script in `host/nook-py`.

---

### Task 1: Add failing host tests for the public binding

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add a host test that:

- builds a loader wrapper through the existing mocked class-loader path
- calls `Java.ClassFactory.get(loader)`
- asserts `typeof factory.openClassFile === 'function'`

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected:

- FAIL because `factory.openClassFile` is missing

**Step 3: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Add failing host tests for validation and loader-scoped behavior

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add host tests proving:

- `factory.openClassFile(123)` throws a type error
- `factory.openClassFile(path).load()` returns a loader wrapper
- `load()` uses the current factory loader as the parent loader
- `load()` does not require mutating the global default loader

Prefer the same style already used by the existing `Java.openClassFile(...)` tests:

- capture wrapper shape
- capture signature / parent-loader effects through existing mocked invocation plumbing

**Step 2: Run test to verify it fails**

Run:

```powershell
.\build\test_js_runtime_native_attach.exe
```

Expected:

- FAIL because loader-scoped `openClassFile(...)` behavior is not implemented

**Step 3: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Implement minimal bootstrap support

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\agent_runtime\js_runtime.cpp`

**Step 1: Write minimal implementation**

Inside `Java.ClassFactory.get(loader)`, extend the returned object with:

- `openClassFile: function (filePath) { ... }`

Implementation requirements:

- validate `filePath` is a string
- return an object exposing `load()`
- `load()` must:
  - obtain `Application` from `android.app.ActivityThread.currentApplication()`
  - obtain `codeCachePath` from `app.getCodeCacheDir().getAbsolutePath()`
  - use the current factory loader wrapper as the parent loader
  - construct `dalvik.system.DexClassLoader` using the exact constructor signature already proven by `Java.openClassFile(...)`
  - return the created loader wrapper
- do not call `Java.setClassLoader(...)`

**Step 2: Run test to verify it passes**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected:

- PASS with the new `factory.openClassFile(...)` coverage

**Step 3: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add a real-device smoke script

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_class_factory_open_class_file_smoke.js`

**Step 1: Write the smoke**

Create a device smoke that:

- obtains a real application/path-class loader through `Java.enumerateClassLoaders(...)`
- creates `factory = Java.ClassFactory.get(loader)`
- checks `typeof factory.openClassFile`
- obtains `apkPath` from `app.getPackageCodePath()`
- calls `factory.openClassFile(apkPath).load()`
- validates the returned wrapper is a `dalvik.system.DexClassLoader`
- creates `scopedFactory = Java.ClassFactory.get(returnedLoader)`
- validates `scopedFactory.use('com.demo.target.TextFragment')` succeeds

**Step 2: Build and push Android artifact if C++ changed**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
```

Expected:

- rebuild succeeds
- push succeeds

**Step 3: Run smoke on device**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_class_factory_open_class_file_smoke.js --wait --usb
```

Expected:

- `factory.openClassFile` binding exists
- `load()` returns a `DexClassLoader`
- loader-scoped `use(...)` through the returned loader succeeds

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Update docs and regression notes

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\docs\code_review.md`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\docs\step6.md` if wording should change

**Step 1: Write the documentation delta**

Record:

- why `ClassFactory.openClassFile(...)` was the next Frida-shape alignment step
- why the implementation stayed bootstrap-only
- why this path must not mutate the global default loader
- exact host verification and device smoke output

**Step 2: Run verification commands**

Run:

```powershell
.\build\test_js_runtime_native_attach.exe
```

and the device smoke command above.

Expected:

- host tests pass
- device smoke confirms the loader-scoped workflow

**Step 3: Write minimal documentation updates**

Capture:

- what was added
- what Frida-facing shape is now covered
- what remains intentionally out of scope

**Step 4: Commit**

Skip commit in-session unless explicitly requested.
