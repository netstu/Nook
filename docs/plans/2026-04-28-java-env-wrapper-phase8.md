# Java Env Wrapper Phase 8 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Env.newWeakGlobalRef(obj)` and `Env.deleteWeakGlobalRef(ref)` as the next minimal Frida-aligned JNI reference primitives.

**Architecture:** Extend the existing `Env` wrapper and JNI bridge with a narrow weak-global creation/deletion path, validate inputs explicitly, and keep the actual JNI operations bound to a live local `JavaEnv jenv` at the call site on Android.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JVM helper scope management, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for weak global refs

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`

**Step 1: Write the failing tests**

Add focused coverage for:

- `env.newWeakGlobalRef(obj)` returns a pointer-like value
- the JNI helper receives the original object handle
- `env.deleteWeakGlobalRef(ref)` returns `true`
- invalid input is rejected for both methods

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because the weak-global methods and test hooks do not exist yet

**Step 3: Keep scope narrow**

Do not add local-ref APIs, weak-ref probe helpers, or auto-cleanup in this phase.

### Task 2: Add narrow host test hooks for weak global refs

**Files:**
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the test-only callback contracts**

Add minimal callback types and setter/resetter APIs for:

- JNI `NewWeakGlobalRef`
- JNI `DeleteWeakGlobalRef`

**Step 2: Wire them into runtime state**

Extend the JNI bridge testing state with the new callbacks.

**Step 3: Re-run the desktop binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- build succeeds
- tests still fail because the `Env` methods are not implemented yet

### Task 3: Implement the weak global `Env` methods

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the wrapper methods**

Implement:

- `Env.newWeakGlobalRef(obj)`
- `Env.deleteWeakGlobalRef(ref)`

**Step 2: Preserve Android attach lifetime**

Ensure each JNI weak-ref operation happens while a local `JavaEnv jenv` is alive at the actual call site.

**Step 3: Validate inputs explicitly**

- `newWeakGlobalRef(...)` must require a non-null Java object/reference
- `deleteWeakGlobalRef(...)` must require a non-null pointer-like reference
- reject malformed input with clear type errors

**Step 4: Keep ownership explicit**

Do not register user-created weak refs into script-owned cleanup in this phase.

**Step 5: Run the desktop binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- all new weak-global tests pass
- earlier Env-wrapper regression coverage remains green

### Task 4: Add the Android smoke

**Files:**
- Create: `host/nook-py/java_env_wrapper_phase8_smoke.js`

**Step 1: Write the smoke script**

Print:

- wrapper binding shape
- weak global ref result
- weak global ref deletion result
- `env.exceptionCheck()` result

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

**Step 4: Run device smoke**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_env_wrapper_phase8_smoke.js --wait --usb
```

Expected:

- weak global ref result is non-null
- deletion returns `true`
- `exceptionCheck()` remains `false`

### Task 5: Update regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the new weak-global surface**

Record:

- why weak global refs are safe in the current architecture
- why local refs remain deferred
- why ownership stays explicit

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- device smoke command and result

**Step 3: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If Android validation was part of the pass, ensure the smoke above was run against freshly pushed binaries.
