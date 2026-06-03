# Nook JS Timers Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add Frida-style timer APIs to the Nook QuickJS runtime so scripts can use `setImmediate`, `setTimeout`, `setInterval`, `clearTimeout`, and `clearInterval`.

**Architecture:** Extend `RuntimeState` with script-owned timer records, register five new globals, and drain due timers at existing runtime safe points. Keep the implementation local to `js_runtime.cpp` and reuse existing pending-job execution and script lifecycle cleanup paths.

**Tech Stack:** C++17, QuickJS, existing Nook script runtime test harness

---

### Task 1: Document the design

**Files:**
- Create: `docs/plans/2026-04-30-nook-js-timers-design.md`
- Create: `docs/plans/2026-04-30-nook-js-timers.md`

**Step 1: Write the design doc**

Capture scope, semantics, cleanup, and testing strategy.

**Step 2: Save the implementation plan**

List exact files, TDD order, and validation steps.

### Task 2: Add failing timer tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test for bindings**

Add a script asserting the five globals exist.

**Step 2: Run the focused test binary or targeted test invocation**

Expected: fail because the bindings are missing.

**Step 3: Add failing behavior tests**

Cover:

- `setImmediate` order
- `setTimeout(0)` order
- `clearTimeout`
- `setInterval`
- `clearInterval`

**Step 4: Re-run tests**

Expected: fail for missing APIs or wrong behavior.

### Task 3: Add timer state and cleanup

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add timer record structures to `RuntimeState`**

Track callback, args, delay metadata, and owning script id.

**Step 2: Add helper functions**

Add helpers for:

- creating timers
- canceling timers
- freeing timer values
- cleaning up timers for one script
- cleaning up all timers

**Step 3: Run tests**

Expected: still failing, but compile passes.

### Task 4: Add global timer bindings

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Implement native JS entry points**

Add:

- `JsSetImmediate`
- `JsSetTimeout`
- `JsSetInterval`
- `JsClearTimeout`
- `JsClearInterval`

**Step 2: Register globals in `InstallGlobalBindingsLocked()`**

Make the five APIs available on `globalThis`.

**Step 3: Run tests**

Expected: binding test passes, behavior tests still fail.

### Task 5: Add timer draining and execution

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Implement due-timer execution helper**

Execute callbacks asynchronously under the correct script id.

**Step 2: Integrate with runtime safe points**

Call timer draining from:

- `Evaluate()`
- `DispatchMessage()`
- `CallRpc()`

and any helper used by those paths.

**Step 3: Re-run tests**

Expected: one-shot and repeating timer tests pass.

### Task 6: Add unload cleanup behavior

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Clear script-owned timers on unload**

Ensure timer callbacks never fire after script unload.

**Step 2: Add or complete unload regression test**

Validate pending timers are removed.

**Step 3: Run tests**

Expected: unload cleanup test passes.

### Task 7: Verify and document

**Files:**
- Modify: `docs/NookFramework_Design_Document.md`

**Step 1: Update runtime API documentation**

Document the supported timer APIs and semantics.

**Step 2: Run targeted tests**

Run the native runtime test target covering new timer behavior.

**Step 3: Summarize residual gaps**

Explicitly note that this is timer support, not a full browser event loop.
