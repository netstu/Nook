# Java.performNow Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal `Java.performNow(fn)` that executes immediately without waiting for class-loader ready state.

**Architecture:** Implement `performNow` in the Java bootstrap layer as a JS-only immediate callback wrapper. Reuse the existing JNI attach-on-demand behavior already present in Nook's Java bridge instead of adding a new native bridge.

**Tech Stack:** QuickJS bootstrap in `js_runtime.cpp`, host runtime tests, Android smoke script

---

### Task 1: Add failing host tests for the public binding

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests for:

- `typeof Java.performNow === 'function'`
- passing a non-function throws

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected:

- host tests fail because `Java.performNow` is missing

**Step 3: Write minimal implementation**

Do not add immediate execution behavior yet. Only after red is observed.

**Step 4: Run test to verify failure reason is correct**

Run the same command again.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Add failing host tests for immediate execution semantics

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests proving:

- `Java.performNow(fn)` executes immediately
- callback side effects are observable right away
- it does not require `Java.ready(...)` queueing

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- failure because `performNow` immediate behavior is not implemented

**Step 3: Write minimal implementation**

Implement `Java.performNow = function (fn) { ... }` in bootstrap with:

- function type check
- direct `fn()` call

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- new `performNow` host tests pass

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Add a real-device smoke script

**Files:**
- Create: `host/nook-py/java_perform_now_smoke.js`

**Step 1: Write the failing smoke**

Create a smoke that validates:

- `typeof Java.performNow`
- callback fires immediately
- callback can call Java framework APIs directly

Prefer a minimal framework-safe path like:

- `android.app.ActivityThread.currentApplication()`
- or `java.lang.System.currentTimeMillis()`

**Step 2: Run smoke to verify it fails or is incomplete**

Run on device after build/push:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_perform_now_smoke.js --wait --usb
```

Expected:

- first run reveals any semantic gap

**Step 3: Write minimal smoke adjustments**

Fix only the specific smoke issue found.

**Step 4: Run smoke to verify it passes**

Run the same command again and capture final output.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`
- Modify: `docs/step6.md` if the capability status wording should change

**Step 1: Write the documentation delta**

Record:

- Frida reference semantics used
- chosen minimal implementation
- why no native bridge was added
- exact smoke output

**Step 2: Run verification commands**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

and the device smoke command used above.

**Step 3: Write minimal documentation updates**

Capture:

- what was added
- validated boundary
- what remains intentionally out of scope

**Step 4: Run final verification**

Run fresh:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- host tests pass with new `performNow` coverage

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
