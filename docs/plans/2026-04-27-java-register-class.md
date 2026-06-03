# Java.registerClass Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal `Java.registerClass(spec)` for Android that supports interface-only JS-backed proxy instances through `$new()`.

**Architecture:** Keep the public API small and Frida-like, but implement only the interface/listener path in phase 1. The JS bootstrap will validate `spec` and delegate to a small native bridge that builds a Java `Proxy` object backed by a helper `InvocationHandler`, which forwards calls into the existing QuickJS runtime.

**Tech Stack:** QuickJS, JNI, existing Nook Java JS bridge, Android `java.lang.reflect.Proxy`, host C++ runtime tests

---

### Task 1: Add failing host tests for the public surface

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests for:

- `typeof Java.registerClass === 'function'`
- `Java.registerClass({...}).$new` exists

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected:

- host test binary builds
- tests fail because `Java.registerClass` is missing

**Step 3: Write minimal implementation**

Do not implement behavior yet. Only after red is observed.

**Step 4: Run test to verify it still reflects the missing feature**

Run the same command and capture the failing assertion.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Add failing host tests for native bridge handoff

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/nook_java_js_bridge.h`

**Step 1: Write the failing test**

Add tests that prove:

- `Java.registerClass({ ... }).$new()` calls into a native bridge helper
- interface class names are forwarded
- method table metadata is forwarded

Prefer test doubles through existing bridge dependency injection patterns instead of real Android execution.

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- failure because no register-class bridge exists yet

**Step 3: Write minimal implementation**

Do not implement full behavior yet. Only enough scaffolding to move to the next red/green step.

**Step 4: Run test to verify failure reason is correct**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- failure points specifically at missing register-class bridge wiring

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Add native bridge data structures and test hooks

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Write the failing test**

Add a test asserting the native layer can accept:

- `name`
- one or more interface class names
- JS method names

and produce a retained Java object result record.

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- failure because the record / bridge function does not exist

**Step 3: Write minimal implementation**

Add:

- a `JavaJsRegisteredMethodRecord`
- a `JavaJsRegisterClassRequest`
- a `RegisterJavaClassFn` dependency hook
- a minimal `RegisterJavaClass(...)` wrapper similar to existing bridge entry points

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- new register-class bridge tests pass

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add `Java.registerClass` bootstrap implementation

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add tests asserting:

- invalid `spec` rejects
- `implements` must be non-empty
- `$new()` exists on the returned class-like object
- `$new()` delegates to the native bridge

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- failures because bootstrap does not yet expose or validate `registerClass`

**Step 3: Write minimal implementation**

In the Java bootstrap:

- add `Java.registerClass = function (spec) { ... }`
- validate:
  - `spec` object
  - `spec.name` string
  - `spec.implements` array with at least one item
  - `spec.methods` object
- return:
  - `{ $new: function () { ... } }`

`$new()` should gather metadata and call a new native bridge function.

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- registerClass surface tests pass

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Implement Android proxy creation path

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Create: `src/java_helper/nook/java/NookJsInvocationHandler.java` or equivalent project-local helper source
- Modify: Android build files as needed to package the helper class / dex payload

**Step 1: Write the failing test**

Add a source regression or injectable-path host test covering:

- helper creation path is invoked
- `Proxy.newProxyInstance(...)` dependencies are resolved
- callback id is retained for later invocation dispatch

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- failure because Android proxy creation path is not implemented

**Step 3: Write minimal implementation**

Implement only the interface proxy path:

- create helper handler
- create interface class array
- call `Proxy.newProxyInstance(...)`
- retain and return the proxy object

Do not add:

- `extends`
- fields
- constructor args
- overload declarations

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- registerClass host tests pass

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 6: Wire helper callback dispatch back into JS

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Modify: `src/agent_runtime/nook_java_js_bridge.h`

**Step 1: Write the failing test**

Add tests proving:

- Java method name dispatch selects the matching JS function
- arguments are converted through existing JavaJsValue conversion paths
- return value flows back through the bridge

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- failure because callback dispatch is not wired

**Step 3: Write minimal implementation**

Reuse the existing runtime callback storage pattern:

- store registered-class method table per callback id
- dispatch by method name
- convert args/results through existing JavaJsValue helpers

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- callback dispatch tests pass

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 7: Add a real-device smoke script

**Files:**
- Create: `host/nook-py/java_register_class_smoke.js`
- Modify: `docs/code_review.md`

**Step 1: Write the failing smoke**

Create a smoke using an interface callback target, preferably:

- `android.view.View$OnClickListener`

Expected smoke checkpoints:

- binding exists
- class-like object created
- proxy instance created
- Java callback reaches JS

**Step 2: Run smoke to verify it fails or is incomplete**

Run on device after build/push:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_register_class_smoke.js --wait --usb
```

Expected:

- first run exposes missing Android integration details if any

**Step 3: Write minimal implementation / smoke adjustments**

Only fix the specific gap found in smoke.

**Step 4: Run smoke to verify it passes**

Run the same command and capture final device output.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 8: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`
- Modify: `docs/step6.md` if priority/status wording changes

**Step 1: Write the failing documentation checklist**

Record:

- chosen architecture
- rejected alternatives
- bugs hit during implementation
- final validated boundary

**Step 2: Run verification commands**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

and any additional source regression / Android build commands used during implementation.

**Step 3: Write minimal documentation updates**

Capture:

- what was added
- what remains out of scope
- exact smoke outputs

**Step 4: Run final verification**

Run fresh:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected:

- host tests pass with the new registerClass coverage

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
