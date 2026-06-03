# Nook Java Wrapper Automatic Cleanup Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Automatically release owned Java wrapper handles on script unload and runtime shutdown without changing the existing explicit `$dispose()` API.

**Architecture:** Mirror the existing per-script native allocation cleanup model. Track owned Java handles under the current script, unregister them on explicit release, and bulk release leftovers from `RemoveMessageHandler(...)` and `Shutdown()`. Keep the change narrow and deterministic.

**Tech Stack:** C++17, QuickJS, JNI, Nook JS runtime, desktop runtime tests

---

### Task 1: Add failing cleanup regression tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Write the failing tests**

Add focused tests for:

- unload cleanup
- `registry.Clear()` cleanup
- `JsRuntime::Shutdown()` cleanup
- explicit `$dispose()` avoiding double release on unload

**Step 2: Run test to verify it fails**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected:

- new cleanup tests fail because owned Java handles are not yet released during teardown

### Task 2: Track owned Java handles per script

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add tracking storage**

Add a per-script container for owned Java handles in `RuntimeState`.

**Step 2: Add helper functions**

Add helpers to:

- register an owned handle for the current script
- unregister an owned handle
- free all owned handles for one script

**Step 3: Use helpers from wrapper creation and explicit release**

- register in owning `CreateJavaUseWrapper(...)` paths
- unregister from `JsJavaRelease(...)` after successful release

### Task 3: Hook cleanup into runtime teardown

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Extend `RemoveMessageHandler(...)`**

Release remaining owned Java handles for the target script in both initialized and uninitialized runtime paths.

**Step 2: Extend `Shutdown()`**

Release remaining owned Java handles for every script before dependency tables are reset.

**Step 3: Keep cleanup idempotent**

Ensure explicit `$dispose()` plus unload/shutdown still release each handle at most once.

### Task 4: Verify and document

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Run the desktop regression binary**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- new cleanup tests pass
- existing runtime tests still pass

**Step 2: Document the fix**

Record:

- root cause
- tracking approach
- unload/shutdown cleanup behavior
- double-release prevention
- verification commands
