# Java.registerClass Unsupported-Spec Boundary Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `Java.registerClass(spec)` explicitly reject unsupported Frida-style spec members that Nook's current proxy architecture cannot honor.

**Architecture:** Keep the current proxy-based `registerClass` pipeline intact. Add JS-bootstrap validation that throws on `spec.fields` and `spec.superClass` before any bridge work occurs. This avoids fake compatibility while leaving room for future real implementations.

**Tech Stack:** QuickJS bootstrap string in `js_runtime.cpp`, host-side C++ runtime tests, optional smoke script

---

### Task 1: Add failing host tests for unsupported spec members

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add tests for:

- `Java.registerClass({... fields: {...} ...})` rejects
- `Java.registerClass({... superClass: SomeClass ...})` rejects

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected:

- tests fail because bootstrap currently accepts these keys silently

### Task 2: Add minimal bootstrap validation

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Implement the smallest validation**

In `Java.registerClass = function (spec) { ... }`:

- reject non-null / non-undefined `spec.fields`
- reject non-null / non-undefined `spec.superClass`

Use clear errors that name the unsupported member.

**Step 2: Run tests to verify they pass**

Run the same host test binary again.

Expected:

- new rejection tests pass
- existing registerClass tests remain green

### Task 3: Add convenience smoke and update docs

**Files:**
- Create: `host/nook-py/java_register_class_spec_boundary_smoke.js`
- Modify: `docs/code_review.md`
- Modify: `docs/step7.md`

**Step 1: Add a small smoke**

Use two guarded `try/catch` checks that send the thrown error text for:

- `fields`
- `superClass`

**Step 2: Document the result**

Record:

- what is rejected now
- why this is intentional
- that Nook still uses `Proxy.newProxyInstance(...)`

**Step 3: Re-run verification if needed**

If any cleanup touched runtime behavior, rerun the host binary before closing.
