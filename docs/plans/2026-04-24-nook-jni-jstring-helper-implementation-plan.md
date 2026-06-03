# Nook JNI jstring Helper Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first minimal JavaScript-side JNI helper so scripts can decode `JNIEnv* + jstring` into a JS string through `Nook.Jni.readJStringUtf8(env, jstr)`.

**Status update:** The API surface landed, but the first production implementation was rolled back to a guarded failure mode. The current native-hook pipeline dispatches JS callbacks asynchronously on a runtime thread, so `JNIEnv*` and local `jstring` references captured on the original hook thread are not safe to dereference later. Until hook-thread snapshotting is implemented, the runtime now throws a controlled error instead of attempting the decode and crashing the target app.

**Architecture:** Reuse the existing QuickJS runtime bootstrap and add one small `Nook.Jni` namespace next to `Nook.Native`. Keep the first version read-only and narrow: accept two pointer-like arguments, delegate decoding through a tiny JNI bridge helper, and expose test hooks so host-side unit tests can run without a real Android JVM.

**Tech Stack:** C++, QuickJS, JNI, Android NDK, existing Nook JS runtime test harness

---

### Task 1: Add failing runtime tests for the minimal JNI helper

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_js_runtime_native_attach.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\agent_runtime\js_runtime_test_api.h`

**Step 1: Write the failing tests**

Add tests for:
- `typeof Nook.Jni.readJStringUtf8 === 'function'`
- `Nook.Jni.readJStringUtf8(ptr('0x1111'), ptr('0x2222'))` returns a JS string through a fake JNI reader
- null or invalid pointer arguments throw

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task35.exe
build\test_js_runtime_native_attach_task35.exe
```

Expected: FAIL because `Nook.Jni.readJStringUtf8` does not exist yet.

### Task 2: Add a minimal JNI string reader bridge with test hooks

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\agent_runtime\js_runtime.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\agent_runtime\js_runtime_test_api.h`

**Step 1: Add test-only injection point**

Expose a tiny setter/resetter in `js_runtime_test_api.h` so tests can inject a fake implementation:

```cpp
using JsRuntimeReadJStringUtf8ForTesting =
    bool (*)(uint64_t env_ptr, uint64_t jstring_ptr, std::string* text_out, std::string* error_out);
```

**Step 2: Implement the real bridge**

In `js_runtime.cpp`:
- on Android, convert `env_ptr` to `JNIEnv*` and `jstring_ptr` to `jstring`
- call `GetStringUTFChars(...)`
- copy into `std::string`
- call `ReleaseStringUTFChars(...)`
- on non-Android, fail with a clear error unless a test hook is installed

**Step 3: Add the JS binding**

Bind:

```javascript
Nook.Jni.readJStringUtf8(env, jstr)
```

Validation:
- `env` must be a non-zero pointer
- `jstr` must be a non-zero pointer
- JNI decode failure throws a JS exception with a readable message

### Task 3: Verify green and document the new helper

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\docs\architecture.md`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\README.md`
- Create or modify smoke script only if needed later; first version can stay unit-test-only

**Step 1: Run runtime tests**

Run:

```powershell
build\test_js_runtime_native_attach_task35.exe
```

Expected: PASS

**Step 2: Run Python regression**

Run:

```powershell
python host\nook-py\tests\test_cli.py
```

Expected: PASS

**Step 3: Update docs**

Document:
- `Nook.Jni.readJStringUtf8(env, jstr)` exists
- it is a minimal JNI helper, not the full future `Java.*` API
- it is meant for cases like native hooks receiving `JNIEnv*` and `jstring`

**Step 4: Rebuild Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk -j4
```

Expected: success

**Step 5: Push updated artifacts**

Run:

```powershell
adb push libs/arm64-v8a/libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push libs/arm64-v8a/libnook.so /data/local/tmp/nook/libnook.so
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server && chmod 644 /data/local/tmp/nook/libnook-agent.so /data/local/tmp/nook/libnook.so'"
```

Expected: updated runtime is ready for device validation
