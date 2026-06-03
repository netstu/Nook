# Java Env Wrapper Phase 2 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend `Env` with `exceptionOccurred()`, `exceptionClear()`, and `getObjectClass(obj)` while preserving the corrected Android attach lifetime model from phase 1.

**Architecture:** Keep `Env` small and runtime-backed. Add only narrow JNI operations and keep all Android JNI work inside a live `JavaEnv jenv` scope at the actual call sites. Use minimal host test callbacks instead of broad fake JNI behavior.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JVM helper scope management, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for the new `Env` methods

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`

**Step 1: Write the failing tests**

Add focused coverage for:

- `env.exceptionOccurred()` returns a pointer-like value
- `env.exceptionClear()` returns `true`
- `env.getObjectClass(obj)` returns a pointer-like value
- `env.getObjectClass(...)` rejects non-Java-object input

Use the existing fake Java object wrapper style for the object argument test.

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because the methods and test hooks do not exist yet

**Step 3: Keep scope narrow**

Do not add any `Env` methods outside the approved phase-2 set.

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Implement narrow host test hooks for the new JNI operations

**Files:**
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add test-only callback contracts**

Add minimal callback types and setter/resetter APIs for:

- exception occurred
- exception clear
- get object class

Keep the callback signatures narrow and explicit.

**Step 2: Wire them into the runtime state**

Extend `JniBridgeState` with the new callbacks and expose matching testing setters/resets.

**Step 3: Run the desktop binary and verify failure shifts**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- build succeeds
- tests still fail because `Env` methods themselves are not implemented yet

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Implement the phase-2 `Env` methods in the runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the new wrapper methods**

Implement:

- `Env.exceptionOccurred()`
- `Env.exceptionClear()`
- `Env.getObjectClass(obj)`

Expose them on the existing wrapper factory only.

**Step 2: Preserve Android attach lifetime**

For Android, ensure each real JNI call executes while a local `JavaEnv jenv` is alive.

Do not rely on previously captured `JNIEnv*` handles for actual JNI operations.

**Step 3: Validate `getObjectClass(obj)` input**

Accept only a valid Nook Java object wrapper and reject malformed input with a clear error.

**Step 4: Run the desktop binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- all new phase-2 tests pass
- phase-1 `Env` tests remain green
- existing `Java.vm.perform/getEnv/tryGetEnv` semantics remain green

**Step 5: Refactor only if needed**

Keep cleanup local to `Env` readability and JNI lifetime correctness.

**Step 6: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add an Android smoke for phase 2

**Files:**
- Create: `host/nook-py/java_env_wrapper_phase2_smoke.js`

**Step 1: Write the smoke script**

Print:

- wrapper binding shape
- `env.exceptionOccurred()`
- `env.exceptionClear()`
- `env.getObjectClass(...)` for a real Java object wrapper

Keep it focused and avoid expanding into class-name or comparison helpers.

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
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_env_wrapper_phase2_smoke.js --wait --usb
```

Expected:

- `exceptionOccurred()` reports `0x0` initially
- `exceptionClear()` returns `true`
- `getObjectClass(...)` returns a non-null pointer-like value

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the phase-2 surface**

Record:

- why these three methods were selected next
- how Android attach lifetime is preserved at call time
- why comparison helpers remain out of scope

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- device smoke command and result

**Step 3: Keep boundaries explicit**

Document what still remains out of scope:

- `isSameObject`
- `isInstanceOf`
- class-name helpers
- reference lifetime helpers

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If Android validation was part of the pass, ensure the smoke above was run against freshly pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
