# Java Env Wrapper Phase 7 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Env.newGlobalRef(obj)` and `Env.deleteGlobalRef(ref)` while preserving the corrected Android attach lifetime model and keeping ownership of user-created global refs explicit.

**Architecture:** Keep `Env` runtime-backed and narrow. Add only the JNI global-reference creation/deletion primitives, validate inputs explicitly, and perform the actual JNI ref operations while a local `JavaEnv jenv` is alive at the call site on Android.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JVM helper scope management, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for the phase 7 global-ref primitives

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`

**Step 1: Write the failing tests**

Add focused coverage for:

- `env.newGlobalRef(obj)` returns a pointer-like value
- the JNI helper receives the original object handle for global-ref creation
- `env.deleteGlobalRef(ref)` returns `true`
- invalid input is rejected for both methods

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because the methods and test hooks do not exist yet

**Step 3: Keep scope narrow**

Do not add local-ref or weak-reference helpers, and do not add auto-cleanup for user-created global refs in this phase.

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Implement the narrow host test hooks for JNI global-ref creation/deletion

**Files:**
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the test-only callback contracts**

Add minimal callback types and setter/resetter APIs for:

- JNI `NewGlobalRef`
- JNI `DeleteGlobalRef`

Keep the callback signatures narrow and explicit.

**Step 2: Wire them into runtime state**

Extend `JniBridgeState` with the callbacks and expose matching testing setters/resets.

**Step 3: Run the desktop binary and verify failure shifts**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- build succeeds
- tests still fail because the `Env` methods themselves are not implemented yet

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Implement the phase 7 `Env` methods in the runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the wrapper methods**

Implement:

- `Env.newGlobalRef(obj)`
- `Env.deleteGlobalRef(ref)`

Expose them on the existing wrapper factory only.

**Step 2: Preserve Android attach lifetime**

For Android, ensure each real JNI ref operation executes while a local `JavaEnv jenv` is alive.

Do not rely on previously captured `JNIEnv*` handles for the actual JNI work.

**Step 3: Validate inputs explicitly**

- `newGlobalRef(...)` must require a non-null Java object/reference
- `deleteGlobalRef(...)` must require a non-null pointer-like reference
- reject malformed input with clear type errors

**Step 4: Keep ownership explicit**

Do not register user-created global refs from `env.newGlobalRef(...)` into script-owned auto-cleanup in this phase.

**Step 5: Run the desktop binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- all new phase-7 tests pass
- phase-1 through phase-6 Env tests remain green
- existing `Java.vm.perform/getEnv/tryGetEnv` semantics remain green

**Step 6: Refactor only if needed**

Keep cleanup local to `Env` readability and JNI lifetime correctness.

**Step 7: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add an Android smoke for phase 7

**Files:**
- Create: `host/nook-py/java_env_wrapper_phase7_smoke.js`

**Step 1: Write the smoke script**

Print:

- wrapper binding shape
- `env.newGlobalRef(...)` result
- `env.deleteGlobalRef(...)` result
- `env.exceptionCheck()` result after deletion

Keep it focused on the stable global-ref path only.

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
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_env_wrapper_phase7_smoke.js --wait --usb
```

Expected:

- global ref result is non-null
- global ref deletion returns `true`
- `exceptionCheck()` remains `false`

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the phase-7 surface**

Record:

- why global refs were selected and local refs deferred
- why user-created global refs remain explicitly caller-owned in this phase
- how Android attach lifetime is preserved at call time
- the root cause that makes local refs unsafe as cross-call `Env` handles in the current architecture

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- device smoke command and result

**Step 3: Keep boundaries explicit**

Document what remains out of scope:

- `newLocalRef(...)`
- `deleteLocalRef(...)`
- `newWeakGlobalRef(...)`
- `deleteWeakGlobalRef(...)`
- auto-cleanup for user-created global refs

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If Android validation was part of the pass, ensure the smoke above was run against freshly pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
