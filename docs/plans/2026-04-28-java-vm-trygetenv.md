# Java.vm.tryGetEnv Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal `Java.vm.tryGetEnv()` entrypoint that returns the current thread's `JNIEnv*` as a `NativePointer`, or `null` when the thread is not attached.

**Architecture:** Extend the internal env-query path so `Java.vm.getEnv()` remains strict while `Java.vm.tryGetEnv()` becomes a non-attaching probe. Keep scope narrow: no env wrapper object, no JNI helper surface, no behavior changes to `Java.vm.perform(fn)`.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, runtime test API, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for the public `Java.vm.tryGetEnv` surface

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add focused coverage for:

- `typeof Java.vm.tryGetEnv === 'function'`
- attached case returns a `NativePointer`
- unavailable case returns `null`

Use the test-only env callback to model both states explicitly.

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new `Java.vm.tryGetEnv()` tests fail because the binding and/or null semantics are missing

**Step 3: Do not touch production code yet**

Only proceed after the failure is observed.

**Step 4: Re-run if needed to confirm the failure reason**

Run the same binary again if needed.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Add failing desktop tests for `Java.vm.perform(...)` compatibility

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add one focused regression proving:

- inside `Java.vm.perform(...)`, `Java.vm.tryGetEnv()` returns a non-null `NativePointer`

Keep the assertion narrow and do not broaden into detached-thread scheduling behavior.

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new compatibility test fails because `tryGetEnv()` is still missing

**Step 3: Keep scope narrow**

Do not add `Env` wrapper expectations or JNI dereference checks.

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Implement the minimal runtime-backed `Java.vm.tryGetEnv()`

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: any tiny supporting file only if test-driven discovery requires it

**Step 1: Refine the internal env-query helper**

Introduce the smallest refactor needed so the helper can support:

- strict env lookup for `getEnv()`
- non-attaching env query for `tryGetEnv()`

**Step 2: On Android, query without attaching**

Implement the `tryGetEnv()` path so it:

- obtains `JavaVM*`
- calls `GetEnv(...)` directly
- returns `null` on `JNI_EDETACHED`
- returns a `NativePointer` on `JNI_OK`
- throws only for unexpected internal failures

**Step 3: Expose `Java.vm.tryGetEnv()`**

Add the runtime-backed function on `Java.vm`:

- no arguments
- return `NativePointer | null`

**Step 4: Extend the test-only callback contract**

Allow host tests to express:

- env available
- env unavailable
- hard failure

Keep the API narrowly scoped to env querying only.

**Step 5: Run the desktop test binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new `Java.vm.tryGetEnv()` tests pass
- existing `Java.vm.getEnv()` / `Java.vm.perform(...)` tests remain green

**Step 6: Refactor only if needed**

Keep cleanup local to helper readability and shared semantics.

**Step 7: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add a focused Android smoke script

**Files:**
- Create: `host/nook-py/java_vm_trygetenv_smoke.js`

**Step 1: Write the smoke script**

The smoke should print:

- binding existence
- direct `tryGetEnv()` result
- `Java.vm.perform(...)`-scoped `tryGetEnv()` result

Prefer stable pointer-only checks such as:

- `env === null`
- `env.isNull()`
- `env.toString()`

**Step 2: Rebuild Android artifacts if runtime code changed**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

Expected:

- Android build succeeds

**Step 3: Push fresh binaries to device**

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
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_vm_trygetenv_smoke.js --wait --usb
```

Expected:

- `Java.vm.tryGetEnv` binding exists
- attached-thread result is non-null directly or from inside `Java.vm.perform(...)`

**Step 5: Keep interpretation narrow**

This smoke only proves attached-thread success behavior, not the detached-thread null case.

**Step 6: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the feature**

Record:

- chosen `tryGetEnv()` semantics
- how it differs from `getEnv()`
- how it aligns with Frida's `Java.vm` split

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- device smoke command and result

**Step 3: Keep boundaries explicit**

Document what remains out of scope:

- env wrapper objects
- JNI helper APIs on top of the returned env pointer
- detached-thread lifecycle orchestration beyond null-return semantics

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If runtime code changed and device validation was part of the pass, ensure the smoke above was run against fresh pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
