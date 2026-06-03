# Nook Java Wrapper $dispose Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add minimal Frida-style explicit disposal for Java instance wrappers through `$dispose()`.

**Architecture:** Keep the change narrow. Add one native release dependency beside `retain`, then expose `$dispose()` only on instance wrappers that own retained global refs. Reuse existing invalid-handle behavior after disposal instead of adding a second error model.

**Tech Stack:** C++17, QuickJS, JNI, existing Nook Java bridge and JS runtime tests

---

### Task 1: Add failing desktop tests for `$dispose()`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Write the failing test**

Add focused tests for:

- owned instance wrapper exposes `$dispose()`
- `$dispose()` releases the owned handle exactly once
- repeated `$dispose()` is idempotent
- instance method call after disposal fails because the handle is invalid

Add fake release capture plumbing in the test file.

**Step 2: Run test to verify it fails**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected:

- compile or runtime failure because release plumbing and `$dispose()` do not exist yet

### Task 2: Add release dependency plumbing

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Keep the Task 1 tests red**

Use the new tests as the active failure signal.

**Step 2: Add the dependency type and helper declaration**

Add:

- `ReleaseJavaObjectFn`
- one field in `JavaJsHookInstallerDependencies`
- `ReleaseJavaObject(...)`

**Step 3: Implement the helper**

Behavior:

- use dependency override if present
- on Android default path, release the global ref with `DeleteGlobalRef()`
- on non-Android without override, report that release is not configured

**Step 4: Run test to verify it still fails**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- tests still fail because JS wrappers do not expose `$dispose()` yet

### Task 3: Add JS wrapper `$dispose()` semantics

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Implement the minimum wrapper ownership model**

In Java wrappers:

- class wrappers use `__nookJavaOwnedHandle = false`
- object wrappers built from retained/global handles use `__nookJavaOwnedHandle = true`

**Step 2: Expose `$dispose()` on wrappers**

Behavior:

- return `undefined`
- if no owned handle remains, no-op
- otherwise call the internal native release helper
- clear the receiver handle and ownership flag

**Step 3: Reuse existing invalid-handle behavior**

Do not add a new disposal-specific error path. Later instance method dispatch should fail naturally through existing invalid-handle checks.

**Step 4: Run test to verify it passes**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- new `$dispose()` regression tests pass

### Task 4: Update progress notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the problem and fix**

Record:

- missing explicit wrapper cleanup compared with Frida
- the new release dependency
- owned-wrapper-only `$dispose()` semantics
- idempotent behavior
- focused test/build verification

**Step 2: Re-run the focused verification command**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- still green after doc update
