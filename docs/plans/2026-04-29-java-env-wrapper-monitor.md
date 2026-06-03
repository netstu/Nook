# Java Env Wrapper Monitor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Env.monitorEnter(obj)` and `Env.monitorExit(obj)` as the next minimal Frida-aligned JNI monitor primitives.

**Architecture:** Extend the existing `Env` wrapper and JNI bridge with two narrow monitor operations. Keep the public API wrapper-first, validate inputs explicitly, and execute the real JNI monitor operations while a live local `JavaEnv jenv` exists at the actual Android call site.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JNI bridge test hooks in `js_runtime_test_api.h`, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for the monitor pair

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`

**Step 1: Write the failing tests**

Add focused coverage for:

- `env.monitorEnter(obj)` returns `true`
- `env.monitorExit(obj)` returns `true`
- the JNI helper receives the original object handle for both methods
- invalid input is rejected for both methods

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because the monitor methods and test hooks do not exist yet

**Step 3: Keep scope narrow**

Do not add `getSuperclass`, `isAssignableFrom`, or any JS helper in this phase.

### Task 2: Add narrow host test hooks for `MonitorEnter` / `MonitorExit`

**Files:**
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the test-only callback contracts**

Add minimal callback types and setter/resetter APIs for:

- JNI `MonitorEnter`
- JNI `MonitorExit`

**Step 2: Wire them into runtime state**

Extend the JNI bridge testing state with the new callbacks.

**Step 3: Re-run the desktop binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- build succeeds
- tests still fail because the public `Env` methods are not implemented yet

### Task 3: Implement the public `Env` methods

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the wrapper methods**

Implement:

- `Env.monitorEnter(obj)`
- `Env.monitorExit(obj)`

Keep both methods wrapper-first and reject non-Java-object input.

**Step 2: Preserve Android attach lifetime**

Ensure each real JNI monitor operation executes while a local `JavaEnv jenv` is alive at the actual call site.

**Step 3: Validate inputs explicitly**

- `monitorEnter(...)` must require a non-null Java object wrapper
- `monitorExit(...)` must require a non-null Java object wrapper
- reject malformed input with clear `TypeError`s

**Step 4: Keep behavior narrow**

- both methods return `true` on success
- no helper-layer `try/finally` abstraction
- no extra ownership or cleanup semantics

**Step 5: Run the desktop binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- all new monitor tests pass
- previous `Env` wrapper tests remain green

### Task 4: Add the Android smoke

**Files:**
- Create: `host/nook-py/java_env_wrapper_monitor_smoke.js`

**Step 1: Write the smoke script**

Print:

- wrapper binding shape
- `monitorEnter(...)` result
- `monitorExit(...)` result
- `env.exceptionCheck()` result

Use a real Java instance wrapper chosen on-device or constructed through the existing surface.

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
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_env_wrapper_monitor_smoke.js --wait --usb
```

Expected:

- bindings exist
- `monitorEnter(...)` returns `true`
- `monitorExit(...)` returns `true`
- `exceptionCheck()` remains `false`

### Task 5: Update regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the new monitor surface**

Record:

- why monitors were selected before `getSuperclass` / `isAssignableFrom`
- why this phase stayed low-level and did not add a JS helper
- why monitors are safe in the current `Env` architecture while local refs are not

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

## Status Update

Superseded on 2026-04-29 after device validation.

Actual outcome:

- the planned desktop path succeeded
- device diagnostics disproved the architecture assumption behind the monitor pair
- the runtime surface was rolled back instead of shipped

Follow-up:

- see [2026-04-29-java-env-wrapper-monitor-postmortem.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-04-29-java-env-wrapper-monitor-postmortem.md)
