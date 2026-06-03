# Java Env Wrapper Phase 3 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Env.isSameObject(a, b)` while preserving the corrected Android attach lifetime model from phases 1 and 2.

**Architecture:** Keep `Env` runtime-backed and narrow. Add only the JNI object-identity helper, parse both arguments as Java object wrappers, and perform the actual JNI call while a local `JavaEnv jenv` is alive at the call site on Android.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JVM helper scope management, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for `Env.isSameObject(a, b)`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`

**Step 1: Write the failing tests**

Add focused coverage for:

- `env.isSameObject(left, right)` returns a boolean
- the JNI helper receives both Java object handles
- non-Java-object input is rejected

Use the existing fake Java object wrapper style for both arguments.

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c .\build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because the method and test hook do not exist yet

**Step 3: Keep scope narrow**

Do not add `isInstanceOf(...)` or any other new `Env` method in this phase.

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Implement the narrow host test hook for object identity

**Files:**
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the test-only callback contract**

Add one minimal callback type and setter/resetter API for:

- JNI `is same object`

Keep the callback signature narrow and explicit:

- env pointer
- left object handle
- right object handle
- boolean result output

**Step 2: Wire it into runtime state**

Extend `JniBridgeState` with the callback and expose matching testing setters/resets.

**Step 3: Run the desktop binary and verify failure shifts**

Run:

```powershell
cmd /c .\build\test_js_runtime_native_attach.exe
```

Expected:

- build succeeds
- tests still fail because the `Env` method itself is not implemented yet

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Implement `Env.isSameObject(a, b)` in the runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the wrapper method**

Implement:

- `Env.isSameObject(a, b)`

Expose it on the existing wrapper factory only.

**Step 2: Preserve Android attach lifetime**

For Android, ensure the actual JNI `IsSameObject(...)` call executes while a local `JavaEnv jenv` is alive.

Do not rely on previously captured `JNIEnv*` handles for the actual JNI operation.

**Step 3: Validate both arguments**

Accept only valid Nook Java object wrappers and reject malformed input with a clear type error.

**Step 4: Run the desktop binary**

Run:

```powershell
cmd /c .\build\test_js_runtime_native_attach.exe
```

Expected:

- all new phase-3 tests pass
- phase-1 and phase-2 `Env` tests remain green
- existing `Java.vm.perform/getEnv/tryGetEnv` semantics remain green

**Step 5: Refactor only if needed**

Keep cleanup local to `Env` readability and JNI lifetime correctness.

**Step 6: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add an Android smoke for phase 3

**Files:**
- Create: `host/nook-py/java_env_wrapper_phase3_smoke.js`

**Step 1: Write the smoke script**

Print:

- wrapper binding shape
- same-object result for `instance` vs `Java.retain(instance)`
- different-object result for `instance` vs another fresh instance

Keep it focused and avoid pulling `isInstanceOf(...)` into this phase.

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
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_env_wrapper_phase3_smoke.js --wait --usb
```

Expected:

- same-object result is `true`
- different-object result is `false`

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the phase-3 surface**

Record:

- why `isSameObject(...)` was selected before `isInstanceOf(...)`
- why wrapper identity must not collapse into raw handle equality
- how Android attach lifetime is preserved at call time

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- device smoke command and result

**Step 3: Keep boundaries explicit**

Document what remains out of scope:

- `isInstanceOf(...)`
- class-name helpers
- reference lifetime helpers

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c .\build\test_js_runtime_native_attach.exe
```

If Android validation was part of the pass, ensure the smoke above was run against freshly pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
