# Java Env Wrapper Phase 1 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace raw env-pointer returns with a first minimal `Env` wrapper and expose `handle`, `toString()`, `exceptionCheck()`, and `findClass(name)`.

**Architecture:** Return an `Env` wrapper from `Java.vm.getEnv()` and `Java.vm.tryGetEnv()`. Keep the wrapper small and runtime-backed. Use narrow test-only callbacks for the first JNI-style methods instead of trying to emulate full JNI on the desktop host.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JVM helpers, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for the new `Env` return shape

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add focused coverage proving:

- `Java.vm.getEnv()` returns an object, not a raw pointer
- `env.handle` is a `NativePointer`
- `env.toString()` includes the pointer value
- `Java.vm.tryGetEnv()` returns either `null` or the same wrapper shape

Keep the assertions narrow and explicit.

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- tests fail because `getEnv()/tryGetEnv()` still return raw pointer values

**Step 3: Do not implement production code yet**

Only proceed after the failure is observed.

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Add failing desktop tests for the first wrapper methods

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h` if additional narrow test hooks are needed

**Step 1: Write the failing tests**

Add focused coverage for:

- `env.exceptionCheck()` returns boolean
- `env.findClass("java/lang/String")` returns a wrapped pointer-like value
- non-string `findClass(...)` rejects input

Use test-only callbacks for these operations so the desktop host stays deterministic.

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new wrapper-method tests fail because the methods do not exist yet

**Step 3: Keep the scope narrow**

Do not add any extra env methods in this task.

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Implement the minimal `Env` wrapper in the runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: any tiny supporting file only if test-driven discovery requires it

**Step 1: Add the wrapper factory**

Implement the smallest internal helper needed to build an `Env` JS object from `JNIEnv*`.

The wrapper should expose:

- `handle`
- `toString()`
- `exceptionCheck()`
- `findClass(name)`

**Step 2: Change `getEnv()/tryGetEnv()` return values**

Make:

- `Java.vm.getEnv()` return `Env`
- `Java.vm.tryGetEnv()` return `Env | null`

**Step 3: Add narrow test-only callbacks for wrapper methods**

Provide only the minimum host hooks needed for:

- exception check
- class lookup

Do not add broad fake JNI plumbing.

**Step 4: Run the desktop test binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- all new `Env` wrapper tests pass
- existing `Java.vm.perform/getEnv/tryGetEnv` semantics remain green

**Step 5: Refactor only if needed**

Keep cleanup local to wrapper readability.

**Step 6: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add a focused Android smoke script

**Files:**
- Create: `host/nook-py/java_env_wrapper_phase1_smoke.js`

**Step 1: Write the smoke script**

The smoke should print:

- wrapper binding shape
- `env.handle`
- `env.exceptionCheck()`
- one successful `env.findClass("java/lang/String")`

Keep it narrow and avoid method-call expansion.

**Step 2: Rebuild Android artifacts if runtime code changed**

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
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_env_wrapper_phase1_smoke.js --wait --usb
```

Expected:

- `Env` wrapper exists
- `handle` is visible
- `exceptionCheck()` runs
- `findClass("java/lang/String")` succeeds

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the wrapper model**

Record:

- why raw pointer returns were replaced
- chosen phase-1 wrapper shape
- why only `exceptionCheck()` and `findClass(...)` landed first

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- device smoke command and result

**Step 3: Keep boundaries explicit**

Document what remains out of scope:

- string creation/reading
- reference lifetime helpers
- JNI method invocation via env
- broader exception helpers

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If Android validation was part of the pass, ensure the smoke above was run against fresh pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
