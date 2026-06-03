# Java.perform Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor `Java.perform(fn)` so readiness is still handled by `Java.ready(fn)` while actual callback execution is unified through `Java.vm.perform(fn)`.

**Architecture:** Keep `Java.ready(fn)` as the lifecycle gate and make `Java.perform(fn)` a thin composition layer. Do not broaden scope into `Java.performNow(fn)` or wider `Java.vm` APIs in this pass.

**Tech Stack:** QuickJS bootstrap in `js_runtime.cpp`, desktop runtime tests, existing Java smoke coverage

---

### Task 1: Add failing desktop tests that prove `Java.perform(fn)` now delegates through `Java.vm.perform(fn)`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add focused coverage proving:

- `Java.perform(123)` still throws
- `Java.perform(fn)` still executes immediately when the ready path is immediate
- internal execution is routed through `Java.vm.perform(fn)`

The delegation test should be explicit. For example:

- temporarily replace `Java.vm.perform`
- have it record a marker before calling through
- verify `Java.perform(fn)` goes through that marker

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new delegation test fails because `Java.perform(fn)` still uses its older direct path

**Step 3: Keep the tests narrow**

Do not broaden this task into `performNow` or `Java.ready` redesign.

**Step 4: Re-run if needed to confirm the failure reason**

Run the same command again if the first failure is ambiguous.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Refactor `Java.perform(fn)` in bootstrap

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Implement the smallest bootstrap refactor**

Refactor `Java.perform(fn)` so it:

- validates `fn`
- passes a wrapper callback into `Java.ready(...)`
- calls `Java.vm.perform(fn)` from inside that wrapper

Preserve the current error text:

- `Java.perform requires a function`

**Step 2: Avoid changing unrelated APIs**

Do not refactor:

- `Java.performNow(fn)`
- `Java.ready(fn)`
- `Java.vm.perform(fn)`

unless a test reveals a minimal compatibility fix is required.

**Step 3: Run the desktop test binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- the new `Java.perform(...)` delegation tests pass
- no regressions appear in existing Java tests

**Step 4: Refactor only if needed**

If cleanup is necessary, keep it local to bootstrap readability.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Add or refresh focused smoke coverage

**Files:**
- Modify: `host/nook-py/java_perform_smoke.js`
- or Create: `host/nook-py/java_perform_vm_refactor_smoke.js`

**Step 1: Add a focused smoke**

The smoke should prove:

- `Java.perform(...)` still fires
- callback can still access Java APIs
- no user-facing regression occurred from the internal refactor

Prefer minimal output over deep diagnostics.

**Step 2: If runtime code changed, rebuild Android artifacts**

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

**Step 4: Run the device smoke**

Run a suitable command such as:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_perform_smoke.js --wait --usb
```

or the new focused smoke if one was created.

Expected:

- the callback still runs
- existing Java access still works

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Update docs and verification notes

**Files:**
- Modify: `docs/code_review.md`
- Optionally modify: `host/nook-py/README.md` if the public explanation should be updated now

**Step 1: Document the refactor**

Record:

- the new layering
- that `Java.perform(fn)` now composes:
  - `Java.ready(fn)`
  - `Java.vm.perform(fn)`

**Step 2: Record verification evidence**

Capture:

- desktop test command
- desktop test result
- Android build command if used
- device smoke command and outcome

**Step 3: Keep scope boundaries explicit**

Document that this pass does not yet refactor:

- `Java.performNow(fn)`
- broader `Java.vm` APIs

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If runtime code changed and device validation was part of the pass, also ensure the fresh smoke was run against the pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
