# Java.vm.perform Attach Semantics Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `Java.vm.perform(fn)` ensure the current thread is attached to the JVM before executing `fn`.

**Architecture:** Keep `Java.vm.perform(fn)` as the single VM execution primitive, but strengthen its runtime behavior. Add a minimal Android-only env-ensure step before invoking the JS callback. Do not change public JS signatures or lifecycle policy.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JVM helper in `JVM.cpp/.h`, desktop runtime tests, Android smoke scripts

---

### Task 1: Add failing desktop tests proving callback-time env availability

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add focused coverage proving:

- inside `Java.vm.perform(...)`, `Java.vm.tryGetEnv()` must be non-null
- the callback still executes synchronously

Use the test-only env query callback to model the difference between:

- outside `perform()`: unavailable
- inside `perform()`: available

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new perform-scoped env availability test fails because `Java.vm.perform(...)` still just calls back directly

**Step 3: Do not implement production code yet**

Only proceed after the failure is observed.

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Implement attach-and-execute semantics in the runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `src/java_hook/JVM.h`
- Modify: `src/java_hook/JVM.cpp`

**Step 1: Add the smallest JVM access needed**

Expose the minimum helper needed to query or acquire `JavaVM*` without forcing unrelated runtime changes.

**Step 2: Refine `JsJavaVmPerform(...)`**

On Android:

- ensure the Java runtime is ready
- acquire a `JNIEnv*` through the existing attach-capable path
- throw if that fails
- then invoke the JS callback synchronously

On non-Android hosts:

- preserve current direct-callback behavior so tests can still run with injected env state

**Step 3: Keep public behavior stable**

Do not change:

- `Java.vm.perform(fn)` signature
- `Java.performNow(fn)` bootstrap wrapper
- `Java.perform(fn)` bootstrap wrapper

**Step 4: Run the desktop test binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new perform-env test passes
- existing `getEnv()` / `tryGetEnv()` tests remain green

**Step 5: Refactor only if needed**

Keep cleanup local to runtime readability.

**Step 6: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Re-run focused Android smokes

**Files:**
- Reuse: `host/nook-py/java_vm_perform_smoke.js`
- Reuse: `host/nook-py/java_vm_trygetenv_smoke.js`

**Step 1: Rebuild Android artifacts if runtime code changed**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

**Step 2: Push fresh binaries**

Run:

```powershell
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libc++_shared.so /data/local/tmp/nook/libc++_shared.so
```

**Step 3: Run `java_vm_trygetenv_smoke.js`**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_vm_trygetenv_smoke.js --wait --usb
```

Expected:

- direct result may still be `null`
- perform-scoped result must be non-null

**Step 4: Optionally re-run `java_vm_perform_smoke.js`**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_vm_perform_smoke.js --wait --usb
```

Expected:

- existing callback execution behavior remains unchanged

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Update docs and verification notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the semantic correction**

Record:

- previous behavior: direct synchronous callback only
- corrected behavior: attach-and-execute
- why this was needed for Frida alignment

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- smoke command(s) and observed output

**Step 3: Keep boundaries explicit**

Document that this pass still does not add:

- async Java worker scheduling
- env wrapper objects
- broader VM lifecycle APIs

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If Android validation was part of the pass, ensure the smoke above was run against fresh pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
