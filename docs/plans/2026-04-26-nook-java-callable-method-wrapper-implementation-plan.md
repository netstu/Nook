# Nook Java Callable Method Wrapper Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make Nook Java method wrappers callable like Frida, so `typeof SomeClass.someMethod === "function"` and direct calls invoke the original Java method.

**Architecture:** Keep the JS wrapper factory in `src/agent_runtime/js_runtime.cpp`, but add a narrow native method-invoke bridge in `src/agent_runtime/nook_java_js_bridge.*`. Reuse existing signature parsing and Java value conversion paths instead of building a second Java reflection stack.

**Tech Stack:** C++17, QuickJS, JNI, existing Java JS bridge, existing JS runtime tests, Android NDK.

---

### Task 1: Add failing runtime tests for callable Java method wrappers

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests that expect:

- `typeof Java.use('com.demo.target.LoginFragment').verifyPasswordNative === 'function'`
- `typeof Java.use('com.demo.target.MainActivity').incrementIntercept.overload('int') === 'function'`
- direct invocation of the static overload returns the fake original result

**Step 2: Run test to verify it fails**

Run the focused native-attach runtime test binary and confirm the new assertions fail because wrappers are still plain objects.

**Step 3: Write minimal implementation**

Do not change hook install semantics yet. Only make the wrapper callable enough to satisfy the new tests.

**Step 4: Run test to verify it passes**

Re-run the same test binary and confirm the new assertions pass.

### Task 2: Add a minimal native Java method invoke bridge

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Write the failing test**

Extend the runtime tests so direct method calls actually depend on a native invoke path.

**Step 2: Run test to verify it fails**

Expected failure:

- direct invocation throws because no native invoke bridge exists

**Step 3: Write minimal implementation**

Add:

- a new invoke dependency hook for tests
- a public `InvokeJavaMethod(...)`
- a default Android implementation using:
  - class/method/signature metadata
  - receiver handle for instance methods
  - JNI `GetMethodID` / `GetStaticMethodID`
  - existing JavaJsValue conversion helpers

**Step 4: Run test to verify it passes**

Confirm direct calls now work in runtime tests.

### Task 3: Convert JS method wrappers from objects to function objects

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Use the same runtime tests from task 1 as the red case.

**Step 2: Run test to verify it fails**

Expected failure:

- `typeof wrapper` is still `object`

**Step 3: Write minimal implementation**

In `CreateJavaUseWrapper(...)`:

- build each method wrapper as a function object
- keep metadata and helper APIs on the function object
- route direct invocation through `__nookJavaInvoke(...)`

**Step 4: Run test to verify it passes**

Confirm:

- `typeof wrapper === 'function'`
- `.implementation` still installs
- `.callOriginal(...)` tests still pass

### Task 4: Rebuild Android and refresh smoke expectations

**Files:**
- Modify: `host/nook-py/java_perform_smoke.js`
- Modify: `host/nook-py/java_ready_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/code_review.md`

**Step 1: Update smoke scripts**

Adjust expected `typeof` outputs from `object` to `function` where appropriate.

**Step 2: Rebuild**

Run the Android arm64 build.

**Step 3: Push**

Push `nook-server`, `libnook-agent.so`, and `libnook.so` to the device.

**Step 4: Verify on device**

Run the existing Java smoke commands and confirm callable-wrapper behavior matches the new expectation.
