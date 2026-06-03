# Nook Java Wrapper GC/Finalizer Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add safe GC-driven cleanup for owned Java wrappers by introducing finalizable native wrapper records and deferring JNI release to controlled runtime drain points.

**Architecture:** Refactor Java wrappers so the ownership state lives in a native QuickJS host object instead of only in plain JS properties. Use the finalizer to enqueue release work, not perform JNI release directly. Keep explicit `$dispose()` and unload/shutdown cleanup intact.

**Tech Stack:** C++17, QuickJS, JNI, Nook JS runtime, Python host smoke scripts

---

### Task 1: Add design-safety regression tests before refactor

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Write failing tests for state transitions**

Add focused tests that describe the desired behavior:

- finalizer-enqueued release drains once
- queued release plus `$dispose()` does not double release
- queued release plus unload does not double release
- queued release plus shutdown does not double release

**Step 2: Run the test binary and verify failure**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected:

- new lifecycle tests fail because wrapper records and pending-release draining do not exist yet

### Task 2: Introduce native Java wrapper host records

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add record structures and runtime storage**

Add:

- `JavaWrapperRecord`
- per-script record storage
- record id allocation if needed

**Step 2: Add QuickJS class plumbing**

Add:

- class id
- class definition
- opaque storage helpers

**Step 3: Preserve current wrapper API**

Refactor `CreateJavaUseWrapper(...)` so:

- the public JS API still behaves the same
- ownership state comes from the native host object / record

### Task 3: Add deferred finalizer queueing

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Implement finalizer**

Behavior:

- no-op for non-owning or already released records
- mark queue state once
- enqueue release work

**Step 2: Add drain helper**

Implement:

- queue draining
- released-state update
- double-release prevention

**Step 3: Wire drain helper into safe runtime points**

Add drain calls in a small number of explicit paths:

- script evaluation
- message dispatch
- RPC dispatch
- unload
- shutdown

### Task 4: Preserve explicit and deterministic cleanup

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Rewire `$dispose()`**

Make `$dispose()` operate on the native wrapper record state.

**Step 2: Rewire unload/shutdown cleanup**

Ensure unload/shutdown can release:

- live records
- queued-but-not-yet-drained records

**Step 3: Verify one-release semantics**

Every release source must converge on the same `released` bit.

### Task 5: Add focused diagnostics and smoke scripts

**Files:**
- Modify: `host/nook-py/java_auto_cleanup_diag.js`
- Optionally create: `host/nook-py/java_gc_cleanup_diag.js`

**Step 1: Add a long-running GC-oriented smoke**

The smoke should:

- retain an owned wrapper
- drop JS references
- create allocation pressure or explicit GC trigger if available
- report before/after state through host logs

**Step 2: Keep unload diagnostics intact**

Do not regress the existing attach/spawn unload verification scripts.

### Task 6: Verify locally and on device

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Run desktop tests**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- new lifecycle tests pass

**Step 2: Run host CLI tests**

Run:

```bash
python -m unittest host.nook-py.tests.test_cli
```

Expected:

- wait-mode unload cleanup tests remain green

**Step 3: Rebuild Android artifacts**

Run:

```bash
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

**Step 4: Device validation**

Verify:

- explicit `$dispose()`
- attach unload cleanup
- spawn unload cleanup
- GC/finalizer-driven cleanup in a still-running process

**Step 5: Document finalizer-specific risks and outcomes**

Record:

- why direct finalizer JNI release was avoided
- queue/drain behavior
- any remaining lifecycle gaps
