# Nook Minimal Java.overload Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal `Java.use(...).method.overload(...)` path that binds an exact Java signature for `implementation` install and `callOriginal(...)`.

**Architecture:** Keep wrapper creation in `src/agent_runtime/js_runtime.cpp` and keep descriptor-aware install/original-call flow in the existing Java JS bridge. The default method wrapper continues to use wildcard `*`, while `overload(...)` creates a sibling wrapper with exact signature metadata.

**Tech Stack:** C++17, QuickJS, JNI descriptor mapping, existing `nook_java_js_bridge`, existing JS runtime tests.

---

### Task 1: Add failing tests for overload wrapper selection

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach_task70.exe`

**Step 1: Write the failing test**

Add a test that evaluates:

```javascript
var LoginFragment = Java.use('com.demo.target.LoginFragment');
var selected = LoginFragment.verifyPasswordNative.overload('java.lang.String');
send({
  type: 'send',
  payload:
    typeof selected + ':' +
    String(selected !== LoginFragment.verifyPasswordNative) + ':' +
    selected.$signature
});
```

Expected payload:

```text
object:true:(Ljava/lang/String;)Z
```

**Step 2: Run test to verify it fails**

Run: `build\test_js_runtime_native_attach_task70.exe`

Expected: FAIL because `overload` is not implemented.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- add type-name to descriptor conversion helpers
- add `overload(...typeNames)` on Java method wrapper
- return a sibling wrapper with `$signature`

**Step 4: Run test to verify it passes**

Run: `build\test_js_runtime_native_attach_task70.exe`

Expected: PASS for the new overload selection test.

### Task 2: Add failing tests for exact-signature hook install

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach_task70.exe`

**Step 1: Write the failing test**

Add a test that installs:

```javascript
var LoginFragment = Java.use('com.demo.target.LoginFragment');
LoginFragment.verifyPasswordNative
  .overload('java.lang.String')
  .implementation = function (password) {
    return password;
  };
```

Assert fake install capture uses:

- `class_name == "com.demo.target.LoginFragment"`
- `method_name == "verifyPasswordNative"`
- `signature == "(Ljava/lang/String;)Z"`

**Step 2: Run test to verify it fails**

Run: `build\test_js_runtime_native_attach_task70.exe`

Expected: FAIL because install still uses `*`.

**Step 3: Write minimal implementation**

Update `JsJavaInstallImplementation(...)` to:

- read `$signature` from the selected method wrapper
- use exact signature when present
- fallback to `*` otherwise

**Step 4: Run test to verify it passes**

Run: `build\test_js_runtime_native_attach_task70.exe`

Expected: PASS for exact-signature install capture.

### Task 3: Add failing tests for exact-signature callOriginal

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build\test_js_runtime_native_attach_task70.exe`

**Step 1: Write the failing test**

Add a test that:

```javascript
var LoginFragment = Java.use('com.demo.target.LoginFragment');
var selected = LoginFragment.verifyPasswordNative.overload('java.lang.String');
selected.implementation = function (password) {
  return selected.callOriginal.call(this, password);
};
```

Then invoke the JS callback through the existing test harness and assert fake `call_original_hook` capture uses:

- exact signature `"(Ljava/lang/String;)Z"`
- same class/method record

**Step 2: Run test to verify it fails**

Run: `build\test_js_runtime_native_attach_task70.exe`

Expected: FAIL because selected overload metadata is not preserved into `callOriginal`.

**Step 3: Write minimal implementation**

Update wrapper metadata creation so `callOriginal` on overload-selected wrappers is bound to the exact signature record.

**Step 4: Run test to verify it passes**

Run: `build\test_js_runtime_native_attach_task70.exe`

Expected: PASS for exact-signature original-call dispatch.

### Task 4: Preserve wildcard fallback and document behavior

**Files:**
- Modify: `host/nook-py/README.md`
- Modify: `docs/code_review.md`
- Test: `build\test_js_runtime_native_attach_task70.exe`

**Step 1: Write/update expectations**

Document:

- default wrapper still uses wildcard `*`
- `overload(...)` selects exact signature
- first version supports primitive types and common class names only

**Step 2: Run regression test**

Run: `build\test_js_runtime_native_attach_task70.exe`

Expected: existing Java smoke-style tests still pass.

**Step 3: Record progress**

Append this milestone and constraints to `docs/code_review.md`.

Plan complete and saved to `docs/plans/2026-04-25-nook-java-overload-minimal-implementation-plan.md`. Two execution options:

1. Subagent-Driven (this session) - I dispatch fresh subagent per task, review between tasks, fast iteration
2. Parallel Session (separate) - Open new session with executing-plans, batch execution with checkpoints
