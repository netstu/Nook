# Java.vm.perform Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal `Java.vm.perform(fn)` entrypoint that provides a VM-level Java execution primitive without waiting for app/class-loader readiness.

**Architecture:** Introduce a small runtime-backed `Java.vm.perform(fn)` primitive and expose it through the Java bootstrap. Keep this pass narrow: only `perform(fn)` is added, while `Env` APIs and broader `Java.vm` parity remain out of scope.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, Java bridge/runtime support, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for the public `Java.vm.perform` surface

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add coverage for:

- `typeof Java.vm === 'object'`
- `typeof Java.vm.perform === 'function'`
- `Java.vm.perform(123)` throws `TypeError`

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- tests fail because `Java.vm` / `Java.vm.perform` does not exist yet

**Step 3: Do not implement production code yet**

Only proceed after the failure is observed.

**Step 4: Re-run if needed to confirm the failure reason**

Run the same command again if the first failure is ambiguous.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Add failing desktop tests for execution semantics

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add coverage proving:

- `Java.vm.perform(fn)` executes synchronously
- callback side effects are visible immediately
- callback can access minimal Java bridge functionality

Use one focused behavior test such as:

- `seen.push('inside')` before `seen.push('after')`
- call a safe Java framework API inside the callback

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- new execution tests fail because the runtime implementation is still missing

**Step 3: Keep tests minimal**

Do not broaden into lifecycle or class-loader semantics in this task.

**Step 4: Re-run if needed to ensure the failure is specific**

Run the same binary again if the first failure is not clearly tied to missing `Java.vm.perform(...)`.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Implement the minimal runtime and bootstrap support

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: any small supporting bridge/runtime file only if strictly needed after test-driven discovery

**Step 1: Add the minimal internal execution path**

Implement the smallest runtime-backed path needed for:

- synchronous callback execution
- Java bridge availability on the current thread

Keep the internal surface narrow and private.

**Step 2: Expose `Java.vm` in bootstrap**

Add:

- `Java.vm`
- `Java.vm.perform(fn)`

Behavior:

- validate `fn`
- throw `TypeError` on non-function input
- execute immediately through the new internal path

**Step 3: Run the desktop test binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new `Java.vm.perform(...)` tests pass

**Step 4: Refactor only if needed**

Do not rewrite `Java.performNow(fn)` or `Java.perform(fn)` in this phase unless a test forces a minimal compatibility adjustment.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add a focused Android smoke script

**Files:**
- Create: `host/nook-py/java_vm_perform_smoke.js`

**Step 1: Write the smoke script**

The smoke should print:

- binding existence
- immediate callback execution
- one minimal Java framework call result from inside `Java.vm.perform(...)`

Prefer stable framework-safe checks such as:

- `java.lang.System.currentTimeMillis()`
- `android.app.ActivityThread.currentApplication()`

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
nook-cli spawn com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_vm_perform_smoke.js --resume --wait --usb
```

Expected:

- `Java.vm` and `Java.vm.perform` bindings exist
- callback fires
- Java framework access inside callback works

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`
- Optionally modify: `host/nook-py/README.md` if public scripting surface documentation should be updated now

**Step 1: Document the feature**

Record:

- chosen minimal `Java.vm.perform(fn)` scope
- why `getEnv()` and broader `Java.vm` APIs were deferred
- how this fits the Frida-alignment path

**Step 2: Record verification evidence**

Capture:

- desktop test command
- desktop test result
- Android build command
- device smoke command

**Step 3: Keep the boundary explicit**

Document what is still out of scope:

- `Java.vm.getEnv()`
- full `Java.perform(fn)` refactor
- broader VM lifecycle APIs

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If runtime code changed, also ensure the smoke command above was run against fresh pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
