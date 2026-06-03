# Java Env superclass and assignability Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Env.getSuperclass(classWrapper)` and `Env.isAssignableFrom(targetClassWrapper, sourceClassWrapper)` as the next safe Frida-aligned JNI class-query helpers.

**Architecture:** Extend the existing `Env` wrapper with two class-wrapper-only helpers. Both helpers must execute as single JNI queries under the current re-entry model, and `getSuperclass(...)` must return a normal Nook class wrapper rather than a raw pointer.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JNI bridge test hooks in `js_runtime_test_api.h`, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`

**Step 1: Write the failing tests**

Add focused coverage for:

- `env.getSuperclass(classWrapper)` returns a class wrapper with the expected class name and loader handle
- `env.getSuperclass(classWrapper)` returns `null` when there is no superclass
- `env.isAssignableFrom(targetClassWrapper, sourceClassWrapper)` returns `true`
- invalid inputs are rejected clearly for both helpers

**Step 2: Run the desktop binary to verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because the helpers and host callbacks do not exist yet

### Task 2: Add narrow host test hooks

**Files:**
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add callback contracts**

Add minimal callback types and setter/resetter APIs for:

- Java `GetSuperclass`
- Java `IsAssignableFrom`

`GetSuperclass` must be able to return:

- superclass pointer
- superclass class name

**Step 2: Wire callbacks into runtime state**

Extend the JNI bridge testing state with the new callbacks.

**Step 3: Re-run the desktop binary**

Expected:

- build succeeds
- tests still fail because public `Env` methods are not implemented yet

### Task 3: Implement the public `Env` helpers

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add class-wrapper parsing**

Add a narrow helper for parsing a Java class wrapper:

- `$className`
- `__nookJavaReceiverHandle == 0`
- optional loader handle

**Step 2: Add runtime query helpers**

Implement internal query helpers for:

- `GetSuperclass`
- `IsAssignableFrom`

`GetSuperclass` must resolve the superclass name before returning.

**Step 3: Add JS-facing `Env` methods**

Implement:

- `Env.getSuperclass(classWrapper)`
- `Env.isAssignableFrom(targetClassWrapper, sourceClassWrapper)`

Behavior:

- `getSuperclass(...)` returns a class wrapper or `null`
- `isAssignableFrom(...)` returns `true` / `false`
- both reject non-class-wrapper input with clear `TypeError`

**Step 4: Run the desktop binary**

Expected:

- all new tests pass
- previous `Env` wrapper regressions remain green

### Task 4: Add Android smoke

**Files:**
- Create: `host/nook-py/java_env_wrapper_superclass_smoke.js`

**Step 1: Write the smoke**

Print:

- binding shape
- superclass of `java.lang.String`
- assignability of `Object <- String`

**Step 2: Rebuild Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

**Step 3: Push fresh binaries**

Run:

```powershell
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libc++_shared.so /data/local/tmp/nook/libc++_shared.so
```

**Step 4: Run smoke**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_env_wrapper_superclass_smoke.js --wait --usb
```

Expected:

- `getSuperclass` exists
- `isAssignableFrom` exists
- superclass of `java.lang.String` is `java.lang.Object`
- `Object <- String` is `true`

### Task 5: Update regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Record why these helpers are safe**

Document that:

- they are single-shot JNI queries
- they do not cross the current `Env` architecture boundary
- they were chosen after the monitor rollback

**Step 2: Record verification**

Capture:

- desktop test command and result
- Android build command and result
- device smoke command and result
