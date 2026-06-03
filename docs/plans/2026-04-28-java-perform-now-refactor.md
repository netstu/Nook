# Java.performNow Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor `Java.performNow(fn)` into a thin wrapper over `Java.vm.perform(fn)` while preserving public behavior.

**Architecture:** Keep `Java.vm.perform(fn)` as the single VM/thread execution base. Refactor `Java.performNow(fn)` in bootstrap only, without changing `Java.ready(fn)` or `Java.perform(fn)` behavior.

**Tech Stack:** QuickJS bootstrap in `js_runtime.cpp`, desktop runtime tests, existing device smoke coverage

---

### Task 1: Add failing desktop tests proving Java.performNow delegates through Java.vm.perform

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add focused coverage for:

- `Java.performNow(123)` still throws the same `TypeError`
- `Java.performNow(fn)` still executes immediately
- `Java.performNow(fn)` now routes through `Java.vm.perform(fn)`

Use an explicit delegation test by temporarily wrapping `Java.vm.perform` and observing the marker sequence.

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new delegation test fails because `Java.performNow(fn)` still executes directly

**Step 3: Keep the scope narrow**

Do not broaden into `Java.perform(fn)` or `Java.ready(fn)` changes in this task.

**Step 4: Re-run if needed to confirm the failure reason**

Run the same command again if the failure is ambiguous.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Refactor Java.performNow in bootstrap

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Implement the smallest bootstrap refactor**

Change `Java.performNow(fn)` so it:

- preserves function validation
- preserves the same error text
- calls `Java.vm.perform(fn)` immediately

**Step 2: Avoid unrelated changes**

Do not modify:

- `Java.vm.perform(fn)`
- `Java.perform(fn)`
- `Java.ready(fn)`

unless a test reveals a minimal compatibility issue.

**Step 3: Run the desktop test binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new delegation tests pass
- existing Java tests remain green

**Step 4: Refactor only if needed**

Keep cleanup local to bootstrap readability.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Re-run device smoke to confirm no user-facing regression

**Files:**
- Reuse: `host/nook-py/java_perform_now_smoke.js`

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

**Step 3: Run the device smoke**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_perform_now_smoke.js --wait --usb
```

Expected:

- immediate callback still fires
- Java framework access inside the callback still works

**Step 4: Keep interpretation narrow**

This smoke only needs to prove no public regression after the internal delegation change.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Update docs and verification notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the refactor**

Record:

- that `Java.performNow(fn)` now delegates to `Java.vm.perform(fn)`
- that the Java execution core is now unified

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- device smoke command and result

**Step 3: Keep the scope boundary explicit**

Document that this pass does not add:

- broader `Java.vm` APIs
- any new ready semantics

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If runtime code changed and device validation was part of the pass, ensure the smoke above was run against fresh pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
