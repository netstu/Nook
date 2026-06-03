# Nook Script.bindWeak Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Introduce a Frida-style `Script.bindWeak(...)` runtime primitive that can later power Java GC-driven wrapper cleanup and other binding lifecycle features.

**Architecture:** Build a generic script-owned weak binding registry and a deferred weak-callback dispatch queue. Keep finalizers internal to the runtime implementation and expose a Frida-like script API at the surface.

**Tech Stack:** C++17, QuickJS, Nook JS runtime, Python host smoke scripts

---

### Task 1: Add failing generic weak-binding tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Write failing tests**

Add generic lifecycle tests for:

- `typeof Script.bindWeak`
- `typeof Script.unbindWeak`
- binding callback enqueues and fires once
- unbinding prevents callback
- unload fires pending binding exactly once

**Step 2: Run the binary and verify failure**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because `Script.bindWeak(...)` and its runtime support do not exist yet

### Task 2: Add runtime weak-binding registry

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add record types and storage**

Add:

- `WeakBindingRecord`
- per-script weak binding registry
- binding id allocation
- pending weak-callback queue

**Step 2: Add API bindings**

Expose:

- `Script.bindWeak(...)`
- `Script.unbindWeak(...)`

### Task 3: Add engine-facing weak implementation

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Introduce finalizer-capable internal host objects if needed**

Keep this internal to the weak-binding runtime machinery.

**Step 2: Enqueue weak callbacks instead of running them directly**

Add:

- enqueue-on-finalizer path
- one-shot callback state
- safe drain helper

### Task 4: Add safe drain points

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Drain pending weak callbacks in controlled runtime paths**

Add drains around:

- script evaluation
- message dispatch
- RPC calls
- unload
- shutdown

**Step 2: Ensure unload semantics are deterministic**

Unload should fire any remaining bound weak callbacks exactly once.

### Task 5: Verify generic primitive first

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Run desktop tests**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- weak-binding tests pass

**Step 2: Document the generic lifecycle primitive**

Record:

- API surface
- callback ordering
- unload semantics
- why callback dispatch is deferred

### Task 6: Java integration in a follow-up pass

**Files:**
- Future follow-up:
  - `src/agent_runtime/js_runtime.cpp`
  - `host/nook-py/java_auto_cleanup_diag.js`

**Step 1: Convert Java GC design to consume `Script.bindWeak(...)`**

Do not mix this into the generic primitive implementation task unless the generic weak runtime is already stable.
