# Java.vm.getEnv Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal `Java.vm.getEnv()` entrypoint that returns the current thread's `JNIEnv*` as a `NativePointer`.

**Architecture:** Introduce a small runtime-backed `Java.vm.getEnv()` primitive and keep scope narrow. Reuse the existing `JavaEnv` path on Android, and add a focused test-only env pointer source for host tests. Do not add `tryGetEnv()` or any `Env` wrapper in this pass.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, runtime test API, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for the public `Java.vm.getEnv` surface

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add focused coverage for:

- `typeof Java.vm.getEnv === 'function'`
- `Java.vm.getEnv()` returns a `NativePointer`
- repeated calls return the same pointer

Use a fixed test env pointer so the expectation is exact and stable.

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new `Java.vm.getEnv()` tests fail because the binding does not exist yet

**Step 3: Do not touch production code yet**

Only proceed after the failure is observed.

**Step 4: Re-run if needed to confirm the failure reason**

Run the same binary again if the failure is ambiguous.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Add failing desktop tests for execution compatibility

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add coverage proving:

- `Java.vm.getEnv()` also works inside `Java.vm.perform(...)`
- the returned pointer still behaves like a `NativePointer`

Keep the test minimal and focused on availability, not JNI dereference behavior.

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new compatibility test fails because the runtime implementation is still missing

**Step 3: Keep scope narrow**

Do not add any `tryGetEnv()` or env-wrapper expectations here.

**Step 4: Re-run if needed to ensure the failure is specific**

Run the same binary again if needed.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Implement the minimal runtime-backed `Java.vm.getEnv()`

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: any tiny supporting file only if test-driven discovery makes it necessary

**Step 1: Add the smallest internal env-pointer helper**

Implement a focused helper that:

- on Android, uses the existing `JavaEnv` path and returns `JNIEnv*`
- in host tests, can be overridden through a test-only callback
- reports failure clearly when no env is available

**Step 2: Expose `Java.vm.getEnv()`**

Add the runtime-backed function on `Java.vm`:

- no arguments
- returns `NativePointer`
- throws a runtime error on env acquisition failure

**Step 3: Add test-only injection surface**

Add a narrow runtime test API for setting and resetting the host env pointer source.

**Step 4: Run the desktop test binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new `Java.vm.getEnv()` tests pass
- existing Java tests remain green

**Step 5: Refactor only if needed**

Keep cleanup local to helper readability.

**Step 6: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add a focused Android smoke script

**Files:**
- Create: `host/nook-py/java_vm_getenv_smoke.js`

**Step 1: Write the smoke script**

The smoke should print:

- binding existence
- direct env pointer state
- env pointer state inside `Java.vm.perform(...)`

Prefer stable pointer-only checks such as:

- `typeof Java.vm.getEnv`
- `env.isNull()`
- `env.toString()`
- `env.equals(...)` if needed through pointer comparison helpers already available

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
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_vm_getenv_smoke.js --wait --usb
```

Expected:

- `Java.vm.getEnv` binding exists
- direct env pointer is non-null
- `Java.vm.perform(...)` can call `Java.vm.getEnv()` and get a non-null pointer

**Step 5: Keep interpretation narrow**

This smoke only proves pointer exposure and immediate compatibility, not JNI dereference safety.

**Step 6: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the feature**

Record:

- chosen minimal `Java.vm.getEnv()` scope
- why `tryGetEnv()` and broader env APIs were deferred
- how this extends the Frida-alignment path after `Java.vm.perform(fn)`

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- device smoke command and result

**Step 3: Keep boundaries explicit**

Document what is still out of scope:

- `Java.vm.tryGetEnv()`
- env wrapper objects
- direct JNI helper APIs layered on the returned pointer

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If runtime code changed and device validation was part of the pass, ensure the smoke above was run against fresh pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
